/**
 * Copyright (c) 2016-2018 Bitprim Inc.
 *
 * This file is part of Bitprim.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef BITPRIM_KEOKEN_MANAGER_HPP_
#define BITPRIM_KEOKEN_MANAGER_HPP_

// #include <cstdint>
// #include <memory>
// #include <bitcoin/blockchain.hpp>
// #include <bitcoin/network.hpp>
// #include <bitcoin/bitcoin/multi_crypto_support.hpp>
// #include <bitcoin/node/configuration.hpp>
// #include <bitcoin/node/define.hpp>
// #include <bitcoin/node/sessions/session_block_sync.hpp>
// #include <bitcoin/node/sessions/session_header_sync.hpp>
// #include <bitcoin/node/utility/check_list.hpp>

#include <bitcoin/bitcoin/wallet/payment_address.hpp>
#include <bitcoin/blockchain/interface/block_chain.hpp>
#include <bitcoin/node/define.hpp>

#include <bitprim/keoken/interpreter.hpp>


namespace bitprim {
namespace keoken {


class BCN_API manager {
public:
    using get_assets_by_address_list = std::vector<get_assets_by_address_data>;
    using get_assets_list = std::vector<get_assets_data>;
    using get_all_asset_addresses_list = std::vector<get_all_asset_addresses_data>;

    explicit
    manager(libbitcoin::blockchain::block_chain& chain, size_t keoken_genesis_height);

    // non-copyable class
    manager(manager const&) = delete;
    manager& operator=(manager const&) = delete;

    // Commands
    void initialize_from_blockchain(size_t from_height, size_t to_height);
    // void initialize_from_blockchain(size_t from_height);
    void initialize_from_blockchain();

    // Queries
    get_assets_by_address_list get_assets_by_address(libbitcoin::wallet::payment_address const& addr) const;
    get_assets_list get_assets() const;
    get_all_asset_addresses_list get_all_asset_addresses() const;

private:
    state state_;
    size_t keoken_genesis_height_;
    libbitcoin::blockchain::block_chain& chain_;
    interpreter interpreter_;
};

} // namespace keoken
} // namespace bitprim

#endif //BITPRIM_KEOKEN_MANAGER_HPP_




    // 1. consultar assets para 1 address
    /*/
        input: addr
        out  : lista de 
                    - asset id
                    - asset name
                    - addr is owner of the asset
                    - saldo
    */

    // 2. listado de assets con su amount inicial
    /*/
        input: NADA
        out  : lista de 
                    - asset id
                    - asset name
                    - addr owner
                    - amount
    */

    // 3. listar todas las wallets con tokens en keoken (saldo mayor a cero)
    /*/
        input: NADA
        out  : lista de 
                    - address **
                    - asset id
                    - asset name
                    - addr owner
                    - amount
    */