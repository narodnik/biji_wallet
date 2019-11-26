#include <bitcoin/bitcoin.hpp>
#include <bitcoin/client.hpp>

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
        bcs::wallet::payment_address address(secret);
        addresses.push_back(address);
    }
    // Add a fake address
    bcs::wallet::payment_address fake("1F2CaWdxuVH7yLEVgzGrqWAGPYJPH5DsHN");
    addresses.push_back(fake);
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

int main()
{
    std::vector<bcs::ec_secret> keys;

    while (true)
    {
        std::cout << "MAIN MENU" << std::endl;
        std::cout << "1. New key" << std::endl;
        std::cout << "2. Receive addresses" << std::endl;
        std::cout << "3. Show history and balance" << std::endl;

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
        default:
            // Do nothing.
            break;
        }
    }

    return 0;
}

