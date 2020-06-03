#include <iostream>
#include <biji.hpp>
using namespace biji;

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
    transaction_destination_list destinations;

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

auto get_change(const std::vector<bcs::ec_secret>& keys ) {
	const auto addresses = convert_keys_to_addresses(keys);
	return std::make_tuple(bcs::wallet::payment_address(addresses.front()), 0);
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
			transaction_destination change = get_change(keys);
            const auto tx = build_transaction(destinations, keys, change);
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

