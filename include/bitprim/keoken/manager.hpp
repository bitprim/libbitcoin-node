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

#include <boost/thread.hpp>

#include <bitcoin/bitcoin/message/messages.hpp>
#include <bitcoin/bitcoin/wallet/payment_address.hpp>
#include <bitcoin/blockchain/interface/block_chain.hpp>
#include <bitcoin/node/define.hpp>

#include <bitprim/keoken/interpreter.hpp>
#include <bitprim/keoken/state.hpp>

namespace bitprim {
namespace keoken {

class BCN_API manager {
public:
    using get_assets_by_address_list = state::get_assets_by_address_list;
    using get_assets_list = state::get_assets_list;
    using get_all_asset_addresses_list = state::get_all_asset_addresses_list;

    explicit
    manager(libbitcoin::blockchain::block_chain& chain, size_t keoken_genesis_height);

    // non-copyable class
    manager(manager const&) = delete;
    manager& operator=(manager const&) = delete;

    // Commands
    void initialize_from_blockchain();

    // Queries
    bool initialized() const;
    get_assets_by_address_list get_assets_by_address(libbitcoin::wallet::payment_address const& addr) const;
    get_assets_list get_assets() const;
    get_all_asset_addresses_list get_all_asset_addresses() const;

private:
    void initialize_from_blockchain(size_t from_height, size_t to_height);
    // void initialize_from_blockchain(size_t from_height);
    void for_each_transaction_non_coinbase(size_t height, libbitcoin::chain::block const& block);
    bool handle_reorganized(libbitcoin::code ec, size_t fork_height, libbitcoin::block_const_ptr_list_const_ptr incoming, libbitcoin::block_const_ptr_list_const_ptr outgoing);

    size_t keoken_genesis_height_;
    state state_;
    libbitcoin::blockchain::block_chain& chain_;
    interpreter interpreter_;
    bool initialized_ = false;
    size_t processed_height_;
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