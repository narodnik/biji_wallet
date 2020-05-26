#include <biji.hpp>

namespace biji {

using point_key_tuple =
    std::tuple<bcs::chain::output_point, bcs::ec_secret>;
using point_key_list = std::vector<point_key_tuple>;

// Not testable due to lack of random engine injection.
bcs::data_chunk new_seed(size_t bit_length)
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

bcs::wallet::payment_address convert_key_to_address(
    const bcs::ec_secret& secret)
{
    if (is_testnet)
    {
        bcs::wallet::ec_private privat(
            secret, bcs::wallet::ec_private::testnet);
        return privat;
    }

    return bcs::wallet::ec_private(secret);
}
address_list convert_keys_to_addresses(const keys_list& keys)
{
    address_list addresses;
    for (const auto& secret: keys)
        addresses.push_back(convert_key_to_address(secret));
    return addresses;
}

void save_keys(const keys_list& keys, const std::string& filename)
{
    std::ofstream fstream(filename);
    for (const auto& key: keys)
    {
        const auto key_string = bc::encode_base16(key);
        fstream.write(key_string.data(), key_string.size());
        fstream.put('\n');
    }
}
void load_keys(keys_list& keys, const std::string& filename)
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

std::tuple<std::optional<point_key_list>, uint64_t> 
	select_outputs(const auto& keys, const auto value)
{
    const auto addresses = convert_keys_to_addresses(keys);
    const auto history_result = get_history(addresses);
    uint64_t total = 0;
    if (!history_result)
   		return std::make_tuple(std::nullopt, total);

    std::map<bcs::wallet::payment_address, bcs::ec_secret> keys_map;
    for (auto i = 0; i < keys.size(); ++i)
        keys_map[addresses[i]] = keys[i];

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
   		return std::make_tuple(std::nullopt, total);
break_loop:
    BITCOIN_ASSERT(total >= value);
	return std::make_tuple(unspent, total - value);
}

std::optional<bcs::chain::transaction> build_transaction(
    const transaction_destination_list& destinations, const keys_list& keys,
    const transaction_destination& change)
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
    const auto [unspent, change_value] = select_outputs(keys, sum);
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
	
	// add the change to outputs
	if (change_value > 0){
		outputs.push_back({
			change_value,
			bcs::chain::script::to_pay_key_hash_pattern(std::get<0>(change).hash())
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

void broadcast(const bcs::chain::transaction& tx)
{
    std::cout << std::endl;
    std::cout << "Sending: " << bcs::encode_base16(tx.to_data()) << std::endl;
    std::cout << std::endl;

    // Bound parameters.
    bcc::obelisk_client client(4000, 0);

    std::cout << "Connecting to " << blockchain_server_address
        << "..." << std::endl;
    const auto endpoint = bcs::config::endpoint(blockchain_server_address);

    if (!client.connect(endpoint))
    {
        std::cerr << "Cannot connect to server" << std::endl;
        return;
    }

    std::atomic<bool> is_error = false;

    auto on_error = [&is_error](const bcs::code& code)
    {
        std::cout << "error: " << code.message() << std::endl;
        is_error = true;
    };

    auto on_done = [](const bcs::code&)
    {
        std::cout << "Broadcasted." << std::endl;
    };

    // This validates the tx, submits it to local tx pool, and notifies peers.
    client.transaction_pool_broadcast(on_error, on_done, tx);
    client.wait();
}

} // namespace biji

