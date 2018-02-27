/**
 * Copyright (c) 2011-2017 libbitcoin developers (see AUTHORS)
 *
 * This file is part of libbitcoin.
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
#include <bitcoin/node/protocols/protocol_block_in.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <memory>
#include <string>
#include <boost/format.hpp>
#include <bitcoin/blockchain.hpp>
#include <bitcoin/network.hpp>
#include <bitcoin/node/define.hpp>
#include <bitcoin/node/full_node.hpp>

namespace libbitcoin {
namespace node {

#define NAME "block_in"
#define CLASS protocol_block_in

using namespace bc::blockchain;
using namespace bc::message;
using namespace bc::network;
using namespace std::chrono;
using namespace std::placeholders;

protocol_block_in::protocol_block_in(full_node& node, channel::ptr channel,
    safe_chain& chain)
  : protocol_timer(node, channel, false, NAME),
    node_(node),
    chain_(chain),
    block_latency_(node.node_settings().block_latency()),

    // TODO: move send_headers to a derived class protocol_block_in_70012.
    headers_from_peer_(negotiated_version() >= version::level::bip130),

    // TODO: move send_compact to a derived class protocol_block_in_70014.
    compact_from_peer_(negotiated_version() >= version::level::bip152),

    // This patch is treated as integral to basic block handling.
    blocks_from_peer_(
        negotiated_version() > version::level::no_blocks_end ||
        negotiated_version() < version::level::no_blocks_start),
    CONSTRUCT_TRACK(protocol_block_in)
{
}

// Start.
//-----------------------------------------------------------------------------

void protocol_block_in::start()
{
    // Use timer to drop slow peers.
    protocol_timer::start(block_latency_, BIND1(handle_timeout, _1));

    // TODO: move headers to a derived class protocol_block_in_31800.
    SUBSCRIBE2(headers, handle_receive_headers, _1, _2);

    // TODO: move not_found to a derived class protocol_block_in_70001.
    SUBSCRIBE2(not_found, handle_receive_not_found, _1, _2);
    SUBSCRIBE2(inventory, handle_receive_inventory, _1, _2);
    SUBSCRIBE2(block, handle_receive_block, _1, _2);

    SUBSCRIBE2(compact_block, handle_receive_compact_block, _1, _2);

    // TODO: move send_headers to a derived class protocol_block_in_70012.
    if (headers_from_peer_)
    {
        // Ask peer to send headers vs. inventory block announcements.
        SEND2(send_headers{}, handle_send, _1, send_headers::command);
    }

    // TODO: move send_compact to a derived class protocol_block_in_70014.
    if (compact_from_peer_)
    {
        // TODO: set relay mode in setting, now is high bandwith (true) and version 1 hardcoded
        SEND2((send_compact{true, 1}), handle_send, _1, send_compact::command);
    }

    send_get_blocks(null_hash);
}

// Send get_[headers|blocks] sequence.
//-----------------------------------------------------------------------------

void protocol_block_in::send_get_blocks(const hash_digest& stop_hash)
{
    const auto heights = block::locator_heights(node_.top_block().height());

    chain_.fetch_block_locator(heights,
        BIND3(handle_fetch_block_locator, _1, _2, stop_hash));
}

void protocol_block_in::handle_fetch_block_locator(const code& ec,
    get_headers_ptr message, const hash_digest& stop_hash)
{
    if (stopped(ec))
        return;

    if (ec)
    {
        LOG_ERROR(LOG_NODE)
            << "Internal failure generating block locator for ["
            << authority() << "] " << ec.message();
        stop(ec);
        return;
    }

    if (message->start_hashes().empty())
        return;

    const auto& last_hash = message->start_hashes().front();

    // TODO: move get_headers to a derived class protocol_block_in_31800.
    const auto use_headers = negotiated_version() >= version::level::headers;
    const auto request_type = (use_headers ? "headers" : "inventory");

    if (stop_hash == null_hash)
    {
        LOG_DEBUG(LOG_NODE)
            << "Ask [" << authority() << "] for " << request_type << " after ["
            << encode_hash(last_hash) << "]";
    }
    else
    {
        LOG_DEBUG(LOG_NODE)
            << "Ask [" << authority() << "] for " << request_type << " from ["
            << encode_hash(last_hash) << "] through ["
            << encode_hash(stop_hash) << "]";
    }

    message->set_stop_hash(stop_hash);

    if (use_headers)
        SEND2(*message, handle_send, _1, message->command);
    else
        SEND2(static_cast<get_blocks>(*message), handle_send, _1,
            message->command);
}

// Receive headers|inventory sequence.
//-----------------------------------------------------------------------------

// TODO: move headers to a derived class protocol_block_in_31800.
// This originates from send_header->annoucements and get_headers requests, or
// from an unsolicited announcement. There is no way to distinguish.
bool protocol_block_in::handle_receive_headers(const code& ec,
    headers_const_ptr message)
{
    if (stopped(ec))
        return false;

    // We don't want to request a batch of headers out of order.
    if (!message->is_sequential())
    {
        LOG_WARNING(LOG_NODE)
            << "Block headers out of order from [" << authority() << "].";
        stop(error::channel_stopped);
        return false;
    }

    // There is no benefit to this use of headers, in fact it is suboptimal.
    // In v3 headers will be used to build block tree before getting blocks.
    const auto response = std::make_shared<get_data>();
    message->to_inventory(response->inventories(), inventory::type_id::block);

    // Remove hashes of blocks that we already have.
    chain_.filter_blocks(response, BIND2(send_get_data, _1, response));
    return true;
}

// This originates from default annoucements and get_blocks requests, or from
// an unsolicited announcement. There is no way to distinguish.
bool protocol_block_in::handle_receive_inventory(const code& ec,
    inventory_const_ptr message)
{
    if (stopped(ec))
        return false;

    const auto response = std::make_shared<get_data>();
    message->reduce(response->inventories(), inventory::type_id::block);

    // Remove hashes of blocks that we already have.
    chain_.filter_blocks(response, BIND2(send_get_data, _1, response));
    return true;
}

void protocol_block_in::send_get_data(const code& ec, get_data_ptr message)
{
    if (stopped(ec))
        return;

    if (ec)
    {
        LOG_ERROR(LOG_NODE)
            << "Internal failure filtering block hashes for ["
            << authority() << "] " << ec.message();
        stop(ec);
        return;
    }

    if (message->inventories().empty())
        return;

    ///////////////////////////////////////////////////////////////////////////
    // Critical Section
    mutex.lock_upgrade();
    const auto fresh = backlog_.empty();
    mutex.unlock_upgrade_and_lock();
    //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

    // Enqueue the block inventory behind the preceding block inventory.
    for (const auto& inventory: message->inventories())
        if (inventory.type() == inventory::type_id::block)
            backlog_.push(inventory.hash());

    mutex.unlock();
    ///////////////////////////////////////////////////////////////////////////

    // There was no backlog so the timer must be started now.
    if (fresh)
        reset_timer();

    // inventory|headers->get_data[blocks]
    SEND2(*message, handle_send, _1, message->command);
}

// Receive not_found sequence.
//-----------------------------------------------------------------------------

// TODO: move not_found to a derived class protocol_block_in_70001.
bool protocol_block_in::handle_receive_not_found(const code& ec,
    not_found_const_ptr message)
{
    if (stopped(ec))
        return false;

    if (ec)
    {
        LOG_DEBUG(LOG_NODE)
            << "Failure getting block not_found from [" << authority() << "] "
            << ec.message();
        stop(ec);
        return false;
    }

    hash_list hashes;
    message->to_hashes(hashes, inventory::type_id::block);

    for (const auto& hash: hashes)
    {
        LOG_DEBUG(LOG_NODE)
            << "Block not_found [" << encode_hash(hash) << "] from ["
            << authority() << "]";
    }

    // The peer cannot locate one or more blocks that it told us it had.
    // This only results from reorganization assuming peer is proper.
    // Drop the peer so next channgel generates a new locator and backlog.
    if (!hashes.empty())
        stop(error::channel_stopped);

    return true;
}

// Receive block sequence.
//-----------------------------------------------------------------------------

bool protocol_block_in::handle_receive_block(const code& ec,
    block_const_ptr message)
{
    if (stopped(ec))
        return false;

    ///////////////////////////////////////////////////////////////////////////
    // Critical Section
    mutex.lock();

    auto matched = !backlog_.empty() && backlog_.front() == message->hash();

    if (matched)
        backlog_.pop();

    // Empty after pop means we need to make a new request.
    const auto cleared = backlog_.empty();

    mutex.unlock();
    ///////////////////////////////////////////////////////////////////////////

    // If a peer sends a block unannounced we drop the peer - always. However
    // it is common for block announcements to cause block requests to be sent
    // out of backlog order due to interleaving of threads. This results in
    // channel drops during initial block download but not after sync. The
    // resolution to this issue is use of headers-first sync, but short of that
    // the current implementation performs well and drops peers no more
    // frequently than block announcements occur during initial block download,
    // and not typically after it is complete.
    if (!matched)
    {
        LOG_DEBUG(LOG_NODE)
            << "Block [" << encode_hash(message->hash())
            << "] unexpected or out of order from [" << authority() << "]";
        stop(error::channel_stopped);
        return false;
    }

    message->validation.originator = nonce();
    chain_.organize(message, BIND2(handle_store_block, _1, message));

    // Sending a new request will reset the timer upon inventory->get_data, but
    // we need to time out the lack of response to those requests when stale.
    // So we rest the timer in case of cleared and for not cleared.
    reset_timer();

    if (cleared)
        send_get_blocks(null_hash);

    return true;
}


bool protocol_block_in::handle_receive_compact_block(code const& ec, compact_block_const_ptr message) {
    if (stopped(ec)) {
        return false;
    }

    //TODO(fernando): Validate if High-bandwith was sent OR getdata was sent for the block.
    //                If not, a peer is sending a block that I didn't ask

    
    //the header of the compact block is the header of the block
    auto const& header_temp = message->header();
    //the nonce used to calculate the short id
    auto const nonce = message->nonce();

    auto const& prefiled_txs = message->transactions();
    auto const& short_ids = message->short_ids();
    
    //TODO(fernando): because collisions are possible, we have to use a multi-map
    //TODO(fernando): check if complete mempool siphashing is need it. See margin notes on BIP152 print.
    //                I think it is needed to check if there are collisions on mempool.
    //auto const mempool_tx_map = chain_.get_mempool_mini_hash_map(*message);

    std::vector<mini_hash> missing_tx;
    std::vector<chain::transaction> txs_to_add;

    //for each shortid we need to search in the mempool or with the getblocktxn/blocktxn messages 
    // and place in the first available position
    /*for (auto const& short_id : short_ids) {
   
        auto it = mempool_tx_map.find(short_id);

        if (it != mempool_tx_map.end()) {
           txs_to_add.emplace_back(it->second);
        }
        else{
            missing_tx.emplace_back(short_id);
        }
    }*/

    if (missing_tx.size() == 0) {

        //there are no missing tx, we can contruct the block

        LOG_DEBUG(LOG_NODE) << "compact block -> block hash " << encode_hash(header_temp.hash());
        
         //the list of transactions in the block
        chain::transaction::list txs_temp;
        txs_temp.reserve(prefiled_txs.size() + short_ids.size());

        // -------------------------------------------------------------------------------------
        auto f = std::begin(prefiled_txs);
        auto const l = std::end(prefiled_txs);
        auto sf = std::begin(short_ids);
        auto const sl = std::end(short_ids);

        auto const ttt = std::find_if(f, l, [](prefilled_transaction const& tx) {
            return tx.index() != 0;
        });
        txs_temp.insert(txs_temp.end(), f, ttt);
        f = ttt;

        while (f != l) {

            auto const pos = (std::min)(f->index(), std::distance(sf, sl));
            auto const sto = std::next(sf, pos);
            
            //TODO(fernando): if f->index() is greater than std::distance(sf, sl), there is an error and should be reported, stopping execution...

            std::transform(sf, sto, std::back_inserter(txs_temp), [](compact_block::short_id const& x) {
                //TODO(fernando): do the real transformation
                return transaction{};
            });

            sf = sto;
            ++f;

            auto ttt = std::find_if(f, l, [](prefilled_transaction const& tx) {
                return tx.index() != 0;
            });
            txs_temp.insert(txs_temp.end(), f, ttt);
            f = ttt;
        }
        //Insertar los compactos que resten

        auto const pos = txs_temp.size() - std::distance(sf, sl);
        auto const sto = std::next(sf, pos);
        std::transform(sf, sto, std::back_inserter(txs_temp), [](compact_block::short_id const& x) {
            //TODO(fernando): do the real transformation
            return transaction{};
        });

        //TODO(fernando): if prefiled_txs.size() + short_ids.size() != txs_temp.size(), there is an error

        // -------------------------------------------------------------------------------------

















        int32_t last_idx = -1;
        //First the prefiled transaction goes to the index position defined in prefilled_transaction->index()
        for (auto const& prefiled_tx : prefiled_txs) {
    
            auto const& tx = prefiled_tx.transaction();
            auto idx = prefiled_tx.index();
        
            last_idx += idx+1;

            /*
            dame n elementos de  la otra lista y meetelo en txs_temp
                n = gap

                */

            LOG_DEBUG(LOG_NODE)
                    << "compact block -> hash " <<  encode_hash(tx.hash()) <<   " idx " << idx;

            /*if (last_idx > std::numeric_limits<uint16_t>::max()){
                return READ_STATUS_INVALID;
            
            }
            
            if ((uint32_t)last_idx > cmpctblock.shorttxids.size() + i) {
            // If we are inserting a tx at an index greater than our full list
            // of shorttxids plus the number of prefilled txn we've inserted,
            // then we have txn for which we have neither a prefilled txn or a
            // shorttxid!
                return READ_STATUS_INVALID;
            }*/


             //TODO       
            txs_temp[last_idx] = tx;
        }

    
        //load short id transactions
        for (auto const& tx : txs_to_add) {
        
                  


        
        }


        chain::block tempblock (header_temp, txs_temp);
    
        LOG_INFO(LOG_NODE)
            << "compact block [*******************************************************************].";


        return true;
        //return handle_receive_block(ec,tempblock);

    }
   
       
    //exists missing tx

    return true;
}


// The block has been saved to the block chain (or not).
// This will be picked up by subscription in block_out and will cause the block
// to be announced to non-originating peers.
void protocol_block_in::handle_store_block(const code& ec,
    block_const_ptr message)
{
    if (stopped(ec))
        return;

    const auto hash = message->header().hash();

    // Ask the peer for blocks from the chain top up to this orphan.
    if (ec == error::orphan_block)
        send_get_blocks(hash);

    const auto encoded = encode_hash(hash);

    if (ec == error::orphan_block ||
        ec == error::duplicate_block ||
        ec == error::insufficient_work)
    {
        LOG_DEBUG(LOG_NODE)
            << "Captured block [" << encoded << "] from [" << authority()
            << "] " << ec.message();
        return;
    }

    // TODO: send reject as applicable.
    if (ec)
    {
        LOG_DEBUG(LOG_NODE)
            << "Rejected block [" << encoded << "] from [" << authority()
            << "] " << ec.message();
        stop(ec);
        return;
    }

    const auto state = message->validation.state;
    BITCOIN_ASSERT(state);

    // Show that diplayed forks may be missing activations due to checkpoints.
    const auto checked = state->is_under_checkpoint() ? "*" : "";

    LOG_DEBUG(LOG_NODE)
        << "Connected block [" << encoded << "] at height [" << state->height()
        << "] from [" << authority() << "] (" << state->enabled_forks()
        << checked << ", " << state->minimum_version() << ").";

    report(*message);
}

// Subscription.
//-----------------------------------------------------------------------------

// This is fired by the callback (i.e. base timer and stop handler).
void protocol_block_in::handle_timeout(const code& ec)
{
    if (stopped(ec))
    {
        // This may get called more than once per stop.
        handle_stop(ec);
        return;
    }

    // Since we need blocks do not stay connected to peer in bad version range.
    if (!blocks_from_peer_)
    {
        stop(error::channel_stopped);
        return;
    }

    if (ec && ec != error::channel_timeout)
    {
        LOG_DEBUG(LOG_NODE)
            << "Failure in block timer for [" << authority() << "] "
            << ec.message();
        stop(ec);
        return;
    }

    ///////////////////////////////////////////////////////////////////////////
    // Critical Section
    mutex.lock_shared();
    const auto backlog_empty = backlog_.empty();
    mutex.unlock_shared();
    ///////////////////////////////////////////////////////////////////////////

    // Can only end up here if time was not extended.
    if (!backlog_empty)
    {
        LOG_DEBUG(LOG_NODE)
            << "Peer [" << authority()
            << "] exceeded configured block latency.";
        stop(ec);
    }

    // Can only end up here if peer did not respond to inventory or get_data.
    // At this point we are caught up with an honest peer. But if we are stale
    // we should try another peer and not just keep pounding this one.
    if (chain_.is_stale())
        stop(error::channel_stopped);

    // If we are not stale then we are either good or stalled until peer sends
    // an announcement. There is no sense pinging a broken peer, so we either
    // drop the peer after a certain mount of time (above 10 minutes) or rely
    // on other peers to keep us moving and periodically age out connections.
}

void protocol_block_in::handle_stop(const code&)
{
    LOG_DEBUG(LOG_NETWORK)
        << "Stopped block_in protocol for [" << authority() << "].";
}

// Block reporting.
//-----------------------------------------------------------------------------

inline bool enabled(size_t height)
{
    // Vary the reporting performance reporting interval by height.
    const auto modulus =
        (height < 100000 ? 100 :
        (height < 200000 ? 10 : 1));

    return height % modulus == 0;
}

inline float difference(const asio::time_point& start,
    const asio::time_point& end)
{
    const auto elapsed = duration_cast<asio::microseconds>(end - start);
    return static_cast<float>(elapsed.count());
}

inline size_t unit_cost(const asio::time_point& start,
    const asio::time_point& end, size_t value)
{
    return static_cast<size_t>(std::round(difference(start, end) / value));
}

inline size_t total_cost_ms(const asio::time_point& start,
    const asio::time_point& end)
{
    static constexpr size_t microseconds_per_millisecond = 1000;
    return unit_cost(start, end, microseconds_per_millisecond);
}

void protocol_block_in::report(const chain::block& block)
{
    BITCOIN_ASSERT(block.validation.state);
    const auto height = block.validation.state->height();

    if (enabled(height))
    {
        const auto& times = block.validation;
        const auto now = asio::steady_clock::now();
        const auto transactions = block.transactions().size();
        const auto inputs = std::max(block.total_inputs(), size_t(1));

        // Subtract total deserialization time from start of validation because
        // the wait time is between end_deserialize and start_check. This lets
        // us simulate block announcement validation time as there is no wait.
        const auto start_validate = times.start_check -
            (times.end_deserialize - times.start_deserialize);

        boost::format format("Block [%|i|] %|4i| txs %|4i| ins "
            "%|4i| wms %|4i| vms %|4i| vus %|4i| rus %|4i| cus %|4i| pus "
            "%|4i| aus %|4i| sus %|4i| dus %|f|");

        LOG_INFO(LOG_BLOCKCHAIN)
            << (format % height % transactions % inputs %

            // wait total (ms)
            total_cost_ms(times.end_deserialize, times.start_check) %

            // validation total (ms)
            total_cost_ms(start_validate, times.start_notify) %

            // validation per input (µs)
            unit_cost(start_validate, times.start_notify, inputs) %

            // deserialization (read) per input (µs)
            unit_cost(times.start_deserialize, times.end_deserialize, inputs) %

            // check per input (µs)
            unit_cost(times.start_check, times.start_populate, inputs) %

            // population per input (µs)
            unit_cost(times.start_populate, times.start_accept, inputs) %

            // accept per input (µs)
            unit_cost(times.start_accept, times.start_connect, inputs) %

            // connect (script) per input (µs)
            unit_cost(times.start_connect, times.start_notify, inputs) %

            // deposit per input (µs)
            unit_cost(times.start_push, times.end_push, inputs) %

            // this block transaction cache efficiency (hits/queries)
            block.validation.cache_efficiency);
    }
}

} // namespace node
} // namespace libbitcoin
