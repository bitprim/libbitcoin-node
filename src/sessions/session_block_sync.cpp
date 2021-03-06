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
#include <bitcoin/node/sessions/session_block_sync.hpp>

#include <cstddef>
#include <memory>
#include <bitcoin/blockchain.hpp>
#include <bitcoin/network.hpp>
#include <bitcoin/node/define.hpp>
#include <bitcoin/node/protocols/protocol_block_sync.hpp>
#include <bitcoin/node/full_node.hpp>
#include <bitcoin/node/settings.hpp>
#include <bitcoin/node/utility/check_list.hpp>
#include <bitcoin/node/utility/reservation.hpp>

namespace libbitcoin {
namespace node {

#define CLASS session_block_sync
#define NAME "session_block_sync"

using namespace bc::blockchain;
using namespace bc::config;
using namespace bc::message;
using namespace bc::network;
using namespace std::placeholders;

// The interval in which all-channel block download performance is tested.
static const asio::seconds regulator_interval(5);

session_block_sync::session_block_sync(full_node& network, check_list& hashes,
    fast_chain& chain, const settings& settings)
  : session<network::session_outbound>(network, false),
    chain_(chain),
    reservations_(hashes, chain, settings),
    CONSTRUCT_TRACK(session_block_sync)
{
}

// Start sequence.
// ----------------------------------------------------------------------------

void session_block_sync::start(result_handler handler)
{
    // TODO: create session_timer base class and pass interval via start.
    timer_ = std::make_shared<deadline>(pool_, regulator_interval);
    session::start(CONCURRENT_DELEGATE2(handle_started, _1, handler));
}

void session_block_sync::handle_started(const code& ec, result_handler handler)
{
    if (ec)
    {
        handler(ec);
        return;
    }

    // TODO: expose block count from reservations and emit here.
    LOG_INFO(LOG_NODE)
        << "Getting blocks.";

    // Copy the reservations table.
    const auto table = reservations_.table();

    if (table.empty())
    {
        handler(error::success);
        return;
    }

    if (!reservations_.start())
    {
        LOG_DEBUG(LOG_NODE)
            << "Failed to set write lock.";
        handler(error::operation_failed);
        return;
    }

    const auto complete = synchronize<result_handler>(
        BIND2(handle_complete, _1, handler), table.size(), NAME);

    // This is the end of the start sequence.
    for (const auto row: table)
        new_connection(row, complete);

    ////reset_timer();
}

// Block sync sequence.
// ----------------------------------------------------------------------------

void session_block_sync::new_connection(reservation::ptr row,
    result_handler handler)
{
    if (stopped())
    {
        LOG_DEBUG(LOG_NODE)
            << "Suspending block slot (" << row ->slot() << ").";
        return;
    }

    LOG_DEBUG(LOG_NODE)
        << "Starting block slot (" << row->slot() << ").";

    // BLOCK SYNC CONNECT
    session_batch::connect(BIND4(handle_connect, _1, _2, row, handler));
}

void session_block_sync::handle_connect(const code& ec, channel::ptr channel,
    reservation::ptr row, result_handler handler)
{
    if (ec)
    {
        LOG_DEBUG(LOG_NODE)
            << "Failure connecting block slot (" << row->slot() << ") "
            << ec.message();
        new_connection(row, handler);
        return;
    }

    LOG_DEBUG(LOG_NODE)
        << "Connected block slot (" << row->slot() << ") ["
        << channel->authority() << "]";

    register_channel(channel,
        BIND4(handle_channel_start, _1, channel, row, handler),
        BIND2(handle_channel_stop, _1, row));
}

void session_block_sync::attach_handshake_protocols(channel::ptr channel,
    result_handler handle_started)
{
    // Don't use configured services or relay for block sync.
    const auto relay = false;
    const auto own_version = settings_.protocol_maximum;
    const auto own_services = version::service::none;
    const auto invalid_services = settings_.invalid_services;
    const auto minimum_version = settings_.protocol_minimum;
    const auto minimum_services = version::service::node_network;

    // The negotiated_version is initialized to the configured maximum.
    if (channel->negotiated_version() >= version::level::bip61)
        attach<protocol_version_70002>(channel, own_version, own_services,
            invalid_services, minimum_version, minimum_services, relay)
            ->start(handle_started);
    else
        attach<protocol_version_31402>(channel, own_version, own_services,
            invalid_services, minimum_version, minimum_services)
            ->start(handle_started);
}

void session_block_sync::handle_channel_start(const code& ec,
    channel::ptr channel, reservation::ptr row, result_handler handler)
{
    // Treat a start failure just like a completion failure.
    if (ec)
    {
        handle_channel_complete(ec, row, handler);
        return;
    }

    attach_protocols(channel, row, handler);
}

void session_block_sync::attach_protocols(channel::ptr channel,
    reservation::ptr row, result_handler handler)
{
    if (channel->negotiated_version() >= version::level::bip31)
        attach<protocol_ping_60001>(channel)->start();
    else
        attach<protocol_ping_31402>(channel)->start();

    attach<protocol_address_31402>(channel)->start();
    attach<protocol_block_sync>(channel, row)->start(
        BIND3(handle_channel_complete, _1, row, handler));
}

void session_block_sync::handle_channel_complete(const code& ec,
    reservation::ptr row, result_handler handler)
{
    if (ec)
    {
        // There is no failure scenario, we ignore the result code here.
        new_connection(row, handler);
        return;
    }

    timer_->stop();
    reservations_.remove(row);

    LOG_DEBUG(LOG_NODE)
        << "Completed block slot (" << row->slot() << ")";

    // This is the end of the block sync sequence.
    handler(error::success);
}

void session_block_sync::handle_channel_stop(const code& ec,
    reservation::ptr row)
{
    LOG_INFO(LOG_NODE)
        << "Channel stopped on block slot (" << row->slot() << ") "
        << ec.message();
}

void session_block_sync::handle_complete(const code& ec,
    result_handler handler)
{
    // Always stop but give sync priority over stop for reporting.
    const auto stop = reservations_.stop();

    if (ec)
    {
        LOG_DEBUG(LOG_NODE)
            << "Failed to complete block sync: " << ec.message();
        handler(ec);
        return;
    }

    if (!stop)
    {
        LOG_DEBUG(LOG_NODE)
            << "Failed to reset write lock: " << ec.message();
        handler(error::operation_failed);
        return;
    }

    LOG_DEBUG(LOG_NODE)
        << "Completed block sync.";
    handler(ec);
}

// Timer.
// ----------------------------------------------------------------------------

// private:
void session_block_sync::reset_timer()
{
    if (stopped())
        return;

    timer_->start(BIND1(handle_timer, _1));
}

void session_block_sync::handle_timer(const code& ec)
{
    if (stopped())
        return;

    LOG_DEBUG(LOG_NODE)
        << "Fired session_block_sync timer: " << ec.message();

    ////// TODO: If (total database time as a fn of total time) add a channel.
    ////// TODO: push into reservations_ implementation.
    ////// TODO: add a boolean increment method to the synchronizer and pass here.
    ////const size_t id = reservations_.table().size();
    ////const auto row = std::make_shared<reservation>(reservations_, id);
    ////const synchronizer<result_handler> handler({}, 0, "name", true);
    ////if (add) new_connection(row, handler);
    ////// TODO: drop the slowest channel
    //////If (drop) reservations_.prune();

    reset_timer();
}

} // namespace node
} // namespace libbitcoin
