//------------------------------------------------------------------------------
/*
    This file is part of clio: https://github.com/XRPLF/clio
    Copyright (c) 2023, the clio developers.

    Permission to use, copy, modify, and distribute this software for any
    purpose with or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL,  DIRECT,  INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#pragma once

#include <backend/BackendInterface.h>
#include <rpc/common/Types.h>
#include <rpc/common/Validators.h>

#include <boost/asio/spawn.hpp>

namespace RPCng {
class GatewayBalancesHandler
{
    std::shared_ptr<BackendInterface> sharedPtrBackend_;

public:
    struct Output
    {
        std::string ledgerHash;
        uint32_t ledgerIndex;
        std::string accountID;
        bool overflow = false;
        std::map<ripple::Currency, ripple::STAmount> sums;
        std::map<ripple::AccountID, std::vector<ripple::STAmount>> hotBalances;
        std::map<ripple::AccountID, std::vector<ripple::STAmount>> assets;
        std::map<ripple::AccountID, std::vector<ripple::STAmount>>
            frozenBalances;
        // validated should be sent via framework
        bool validated = true;
    };

    // TODO:we did not implement the "strict" field
    struct Input
    {
        std::string account;
        std::set<ripple::AccountID> hotWallets;
        std::optional<std::string> ledgerHash;
        std::optional<uint32_t> ledgerIndex;
    };

    using Result = RPCng::HandlerReturnType<Output>;

    GatewayBalancesHandler(
        std::shared_ptr<BackendInterface> const& sharedPtrBackend)
        : sharedPtrBackend_(sharedPtrBackend)
    {
    }

    RpcSpecConstRef
    spec() const
    {
        static auto const hotWalletValidator = validation::CustomValidator{
            [](boost::json::value const& value,
               std::string_view key) -> MaybeError {
                if (!value.is_string() && !value.is_array())
                {
                    return Error{RPC::Status{
                        RPC::RippledError::rpcINVALID_PARAMS,
                        std::string(key) + "NotStringOrArray"}};
                }
                // wallet needs to be an valid accountID or public key
                auto const wallets = value.is_array()
                    ? value.as_array()
                    : boost::json::array{value};
                auto const getAccountID =
                    [](auto const& j) -> std::optional<ripple::AccountID> {
                    if (j.is_string())
                    {
                        auto const pk = ripple::parseBase58<ripple::PublicKey>(
                            ripple::TokenType::AccountPublic,
                            j.as_string().c_str());
                        if (pk)
                            return ripple::calcAccountID(*pk);
                        return ripple::parseBase58<ripple::AccountID>(
                            j.as_string().c_str());
                    }
                    return {};
                };
                for (auto const& wallet : wallets)
                {
                    if (!getAccountID(wallet))
                        return Error{RPC::Status{
                            RPC::RippledError::rpcINVALID_PARAMS,
                            std::string(key) + "Malformed"}};
                }
                return MaybeError{};
            }};

        static const RpcSpec rpcSpec = {
            {"account", validation::Required{}, validation::AccountValidator},
            {"ledger_hash", validation::Uint256HexStringValidator},
            {"ledger_index", validation::LedgerIndexValidator},
            {"hotwallet", hotWalletValidator}};
        return rpcSpec;
    }

    Result
    process(Input input, boost::asio::yield_context& yield) const;
};

void
tag_invoke(
    boost::json::value_from_tag,
    boost::json::value& jv,
    GatewayBalancesHandler::Output const& output);

GatewayBalancesHandler::Input
tag_invoke(
    boost::json::value_to_tag<GatewayBalancesHandler::Input>,
    boost::json::value const& jv);
}  // namespace RPCng
