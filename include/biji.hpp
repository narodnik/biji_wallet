#ifndef BIJI_BIJI_HPP
#define BIJI_BIJI_HPP

#include <optional>
#include <string>
#include <bitcoin/bitcoin.hpp>
#include <bitcoin/client.hpp>

namespace biji {

namespace bcs = bc;
namespace bcc = bc::client;

constexpr bool is_testnet = true;

constexpr const char* blockchain_server_address = is_testnet ?
    "tcp://testnet.libbitcoin.net:19091" :
    "tcp://mainnet.libbitcoin.net:9091";

using keys_list = std::vector<bcs::ec_secret>;
using address_list = std::vector<bcs::wallet::payment_address>;

using history_list = bc::chain::history::list;
using history_map = std::map<bcs::wallet::payment_address, history_list>;

using transaction_destination =
    std::tuple<bcs::wallet::payment_address, uint64_t>;
using transaction_destination_list = std::vector<transaction_destination>;

bcs::data_chunk new_seed(size_t bit_length=192);
// The key may be invalid, caller may test for null secret.
bcs::ec_secret new_key(const bcs::data_chunk& seed);
bcs::ec_secret new_key();

bcs::wallet::payment_address convert_key_to_address(
    const bcs::ec_secret& secret);
// Convenience function for convert_key_to_address() on list of keys
address_list convert_keys_to_addresses(const keys_list& keys);

void save_keys(const keys_list& keys, const std::string& filename);
void load_keys(keys_list& keys, const std::string& filename);

std::optional<history_map> get_history(const address_list& addresses);

std::optional<bcs::chain::transaction> build_transaction(
    const transaction_destination_list& destinations, const keys_list& keys,
    const transaction_destination& change);

void broadcast(const bcs::chain::transaction& tx);

} // namespace biji

#endif

