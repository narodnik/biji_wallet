#include <fstream>
#include <bitcoin/bitcoin.hpp>
#include <bitcoin/client.hpp>

#include "config.hpp"
#include "get_history.ipp"

namespace bcs = bc;
namespace bcc = bc::client;

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

auto convert_keys_to_addresses(const auto& keys)
{
    std::vector<bcs::wallet::payment_address> addresses;
    for (const auto& secret: keys)
    {
        if (is_testnet)
        {
            bcs::wallet::ec_private privat(
                secret, bcs::wallet::ec_private::testnet);
            addresses.emplace_back(privat);
        }
        else
            addresses.emplace_back(secret);
    }
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

        std::cout << "Continue? [Y/n] ";
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

std::optional<bcs::chain::point::list> select_outputs(
    const auto& keys, const auto value)
{
    const auto addresses = convert_keys_to_addresses(keys);
    const auto history_result = get_history(addresses);
    if (!history_result)
        return std::nullopt;

    uint64_t total = 0;
    bcs::chain::point::list unspent;
    for (const auto& [address, history]: *history_result)
    {
        for (const auto& row: history)
        {
            // Skip spent outputs
            if (row.spend_height != bcs::max_uint64)
                continue;

            // Unspent output
            total += value;
            unspent.push_back(row.output);

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

bool send_funds(const auto& destinations, const auto& keys)
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
        return false;

    // build tx
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
            send_funds(destinations, keys);
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

