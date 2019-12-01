#include <fstream>
#include <bitcoin/bitcoin.hpp>
#include <bitcoin/client.hpp>
#include "config.hpp"
#include "broadcast.ipp"
#include "get_history.ipp"

namespace bcs = bc;
namespace bcc = bc::client;

using endorsement_list = std::vector<bcs::endorsement>;

// Not testable due to lack of random engine injection.
bcs::data_chunk new_seed(size_t bit_length=192)
{
    size_t fill_seed_size = bit_length / bcs::byte_bits;
    bcs::data_chunk seed(fill_seed_size);
    bcs::pseudo_random_fill(seed);
    return seed;
}

// The key may be invalid, caller may test for null secret.
bcs::ec_secret new_key(const bcs::data_chunk& seed)
{
    const bcs::wallet::hd_private key(seed);
    return key.secret();
}

bcs::ec_secret new_key()
{
    const auto seed = new_seed();
    return new_key(seed);
}

bcs::wallet::payment_address convert_key_to_address(const auto& secret)
{
    if (is_testnet)
    {
        bcs::wallet::ec_private privat(
            secret, bcs::wallet::ec_private::testnet);
        return privat;
    }

    return bcs::wallet::ec_private(secret);
}

auto convert_keys_to_addresses(const auto& keys)
{
    std::vector<bcs::wallet::payment_address> addresses;
    for (const auto& secret: keys)
        addresses.push_back(convert_key_to_address(secret));
    return addresses;
}

void display_history(const auto& histories)
{
    for (const auto& [address, history]: histories)
    {
        std::cout << "== " << address.encoded() << " ==" << std::endl;

        for (const auto& row: history)
        {
            if (row.output_height != bcs::max_uint64)
            {
                std::cout << "+" << bcs::encode_base10(row.value, 8) << "\t"
                    << bcs::encode_base16(row.output.hash()) << ":"
                    << row.output.index() << std::endl;
            }

            if (row.spend_height != bcs::max_uint64)
            {
                std::cout << "-" << bcs::encode_base10(row.value, 8) << "\t"
                    << bcs::encode_base16(row.spend.hash()) << ":"
                    << row.spend.index() << std::endl;
            }
        }
    }
}

auto prompt_destinations()
{
    std::vector<std::tuple<bcs::wallet::payment_address, uint64_t>>
        destinations;

    while (true)
    {
        std::string address;
        std::cout << "Address: ";
        std::cin >> address;

        std::string amount_string;
        std::cout << "Amount: ";
        std::cin >> amount_string;

        uint64_t value;
        auto rc = bcs::decode_base10(value, amount_string, 8);
        BITCOIN_ASSERT(rc);

        destinations.emplace_back(std::make_tuple(
            bcs::wallet::payment_address(address), value));

        std::cout << "Add another output? [Y/n] ";
        std::string is_continue;
        std::cin >> is_continue;
        if (is_continue != "y" && is_continue != "Y")
            break;
    }

    return destinations;
}

void save_keys(const auto& keys, const auto& filename)
{
    std::ofstream fstream(filename);
    for (const auto& key: keys)
    {
        const auto key_string = bc::encode_base16(key);
        fstream.write(key_string.data(), key_string.size());
        fstream.put('\n');
    }
}
void load_keys(auto& keys, const auto& filename)
{
    std::ifstream fstream(filename);
    std::string line;
    while (std::getline(fstream, line))
    {
        bcs::ec_secret secret;
        bool rc = bcs::decode_base16(secret, line);
        BITCOIN_ASSERT(rc);
        keys.push_back(secret);
    }
}

using point_key_tuple =
    std::tuple<bcs::chain::output_point, bcs::ec_secret>;
using point_key_list = std::vector<point_key_tuple>;

std::optional<point_key_list> select_outputs(const auto& keys, const auto value)
{
    const auto addresses = convert_keys_to_addresses(keys);
    const auto history_result = get_history(addresses);
    if (!history_result)
        return std::nullopt;

    std::map<bcs::wallet::payment_address, bcs::ec_secret> keys_map;
    for (auto i = 0; i < keys.size(); ++i)
        keys_map[addresses[i]] = keys[i];

    uint64_t total = 0;
    point_key_list unspent;
    for (const auto& [address, history]: *history_result)
    {
        for (const auto& row: history)
        {
            // Skip spent outputs
            if (row.spend_height != bcs::max_uint64)
                continue;

            const auto& key = keys_map[address];

            // Unspent output
            total += row.value;
            unspent.push_back({ row.output, key });

            if (total >= value)
                goto break_loop;
        }
    }
    if (total < value)
        return std::nullopt;
break_loop:
    BITCOIN_ASSERT(total >= value);
    return unspent;
}

std::optional<bcs::chain::transaction> build_transaction(
    const auto& destinations, const auto& keys)
{
    // sum values in dest
    auto fold = [](uint64_t total,
        std::tuple<bcs::wallet::payment_address, uint64_t> destination)
    {
        return total + std::get<1>(destination);
    };
    auto sum = std::accumulate(
        destinations.begin(), destinations.end(), 0, fold);

    // select unspent outputs where sum(outputs) >= sum
    const auto unspent = select_outputs(keys, sum);
    if (!unspent)
    {
        std::cerr << "Not enough funds for send." << std::endl;
        return std::nullopt;
    }

    // build tx
    bcs::chain::transaction tx;
    tx.set_version(1);
    tx.set_locktime(0);

    bcs::chain::input::list inputs;
    for (const auto& [previous_output, key]: *unspent)
    {
        bcs::chain::input input;
        input.set_previous_output(previous_output);
        input.set_sequence(bcs::max_uint32);
        inputs.push_back(std::move(input));
    }
    tx.set_inputs(inputs);

    bcs::chain::output::list outputs;
    for (const auto& [address, value]: destinations)
    {
        outputs.push_back({
            value,
            bcs::chain::script::to_pay_key_hash_pattern(address.hash())
        });
    }
    tx.set_outputs(std::move(outputs));

    // sign tx
    for (auto i = 0; i < unspent->size(); ++i)
    {
        const auto& [previous_output, key] = unspent->at(i);

        const auto address = convert_key_to_address(key);

        bcs::chain::script prevout_script =
            bcs::chain::script::to_pay_key_hash_pattern(address.hash());

        bcs::endorsement endorsement;
        auto rc = bcs::chain::script::create_endorsement(endorsement,
            key, prevout_script, tx, i, bcs::machine::sighash_algorithm::all);

        bcs::wallet::ec_public public_key(key);

        bcs::chain::script input_script({
            { endorsement },
            bcs::machine::operation({
                public_key.point().begin(), public_key.point().end() })
        });

        inputs[i].set_script(input_script);
    }
    tx.set_inputs(inputs);

    return tx;
}

auto confirm_transaction(const auto& tx)
{
    std::cout << std::endl;
    std::cout << "tx hash: " << bcs::encode_hash(tx.hash()) << std::endl;

    for (const auto& input: tx.inputs())
    {
        const auto& prevout = input.previous_output();
        std::cout << "Prevout: " << bcs::encode_base16(prevout.hash()) << ":"
            << prevout.index() << std::endl;
        std::cout << "Input script: " <<
            input.script().to_string(bcs::machine::rule_fork::all_rules)
            << std::endl;
    }

    for (const auto& output: tx.outputs())
    {
        std::cout << "Value: " << output.value() << std::endl;
        std::cout << "Output script: " <<
            output.script().to_string(bcs::machine::rule_fork::all_rules)
            << std::endl;
    }

    std::cout << "Send transaction? [y/N] ";
    std::string is_continue;
    std::cin >> is_continue;
    return is_continue == "y" || is_continue == "Y";
}

int main()
{
    std::vector<bcs::ec_secret> keys;
    load_keys(keys, "wallet.dat");

    if (is_testnet)
    {
        std::cout << "Running on testnet" << std::endl;

        //bcs::wallet::ec_private privat(
        //    "cVks5KCc8BBVhWnTJSLjr5odLbNrWK9UY4KprciJJ9dqiDBenhzr",
        //    bcs::wallet::ec_private::testnet_p2kh);
        //keys.push_back(privat.secret());
    }

    bool is_exit = false;
    while (!is_exit)
    {
        std::cout << "MAIN MENU" << std::endl;
        std::cout << "1. New key" << std::endl;
        std::cout << "2. Receive addresses" << std::endl;
        std::cout << "3. Show history and balance" << std::endl;
        std::cout << "4. Send funds" << std::endl;
        std::cout << "5. Exit" << std::endl;

        int choice = 0;
        std::cin >> choice;
        switch (choice)
        {
        case 1:
        {
            keys.push_back(new_key());
            break;
        }
        case 2:
        {
            const auto addresses = convert_keys_to_addresses(keys);
            for (const auto& address: addresses)
                std::cout << address.encoded() << std::endl;
            break;
        }
        case 3:
        {
            const auto addresses = convert_keys_to_addresses(keys);
            const auto result = get_history(addresses);
            if (!result)
                break;
            display_history(*result);
            break;
        }
        case 4:
        {
            const auto destinations = prompt_destinations();
            const auto tx = build_transaction(destinations, keys);
            if (!tx)
            {
                std::cerr << "Not enough funds." << std::endl;
                break;
            }
            if (!confirm_transaction(*tx))
                break;
            broadcast(*tx);
            break;
        }
        case 5:
        {
            std::cout << "Saving wallet to wallet.dat" << std::endl;
            save_keys(keys, "wallet.dat");
            is_exit = true;
            break;
        }
        default:
            // Do nothing.
            break;
        }
    }

    return 0;
}

