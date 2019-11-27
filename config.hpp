#ifndef BIJIWALLET_CONFIG_HPP
#define BIJIWALLET_CONFIG_HPP

constexpr auto is_testnet = true;

constexpr auto blockchain_server_address = is_testnet ?
    "tcp://testnet.libbitcoin.net:9091" :
    "tcp://testnet.libbitcoin.net:9091";

#endif // BIJIWALLET_CONFIG_HPP

