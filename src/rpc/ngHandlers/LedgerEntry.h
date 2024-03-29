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

class LedgerEntryHandler
{
    std::shared_ptr<BackendInterface> sharedPtrBackend_;

public:
    struct Output
    {
        std::string index;
        uint32_t ledgerIndex;
        std::string ledgerHash;
        std::optional<boost::json::object> node;
        std::optional<std::string> nodeBinary;
        bool validated = true;
    };

    // TODO: nft_page has not been implemented
    struct Input
    {
        std::optional<std::string> ledgerHash;
        std::optional<uint32_t> ledgerIndex;
        bool binary = false;
        // id of this ledger entry: 256 bits hex string
        std::optional<std::string> index;
        // index can be extracted from payment_channel, check, escrow, offer
        // etc, expectedType is used to save the type of index
        ripple::LedgerEntryType expectedType = ripple::ltANY;
        // account id to address account root object
        std::optional<std::string> accountRoot;
        // TODO: extract into custom objects, remove json from Input
        std::optional<boost::json::object> directory;
        std::optional<boost::json::object> offer;
        std::optional<boost::json::object> rippleStateAccount;
        std::optional<boost::json::object> escrow;
        std::optional<boost::json::object> depositPreauth;
        std::optional<boost::json::object> ticket;
    };

    using Result = RPCng::HandlerReturnType<Output>;

    LedgerEntryHandler(
        std::shared_ptr<BackendInterface> const& sharedPtrBackend)
        : sharedPtrBackend_(sharedPtrBackend)
    {
    }

    RpcSpecConstRef
    spec() const
    {
        // Validator only works in this handler
        // The accounts array must have two different elements
        // Each element must be a valid address
        static auto const rippleStateAccountsCheck =
            validation::CustomValidator{
                [](boost::json::value const& value,
                   std::string_view key) -> MaybeError {
                    if (!value.is_array() || value.as_array().size() != 2 ||
                        !value.as_array()[0].is_string() ||
                        !value.as_array()[1].is_string() ||
                        value.as_array()[0].as_string() ==
                            value.as_array()[1].as_string())
                        return Error{RPC::Status{
                            RPC::RippledError::rpcINVALID_PARAMS,
                            "malformedAccounts"}};
                    auto const id1 = ripple::parseBase58<ripple::AccountID>(
                        value.as_array()[0].as_string().c_str());
                    auto const id2 = ripple::parseBase58<ripple::AccountID>(
                        value.as_array()[1].as_string().c_str());
                    if (!id1 || !id2)
                        return Error{RPC::Status{
                            RPC::ClioError::rpcMALFORMED_ADDRESS,
                            "malformedAddresses"}};
                    return MaybeError{};
                }};

        static const RpcSpec rpcSpec = {
            {"binary", validation::Type<bool>{}},
            {"ledger_hash", validation::Uint256HexStringValidator},
            {"ledger_index", validation::LedgerIndexValidator},
            {"index", validation::Uint256HexStringValidator},
            {"account_root", validation::AccountBase58Validator},
            {"check", validation::Uint256HexStringValidator},
            {"deposit_preauth",
             validation::Type<std::string, boost::json::object>{},
             validation::IfType<std::string>{
                 validation::Uint256HexStringValidator},
             validation::IfType<boost::json::object>{
                 validation::Section{
                     {"owner",
                      validation::Required{},
                      validation::AccountBase58Validator},
                     {"authorized",
                      validation::Required{},
                      validation::AccountBase58Validator},
                 },
             }},
            {"directory",
             validation::Type<std::string, boost::json::object>{},
             validation::IfType<std::string>{
                 validation::Uint256HexStringValidator},
             validation::IfType<boost::json::object>{validation::Section{
                 {"owner", validation::AccountBase58Validator},
                 {"dir_root", validation::Uint256HexStringValidator},
                 {"sub_index", validation::Type<uint32_t>{}}}}},
            {"escrow",
             validation::Type<std::string, boost::json::object>{},
             validation::IfType<std::string>{
                 validation::Uint256HexStringValidator},
             validation::IfType<boost::json::object>{
                 validation::Section{
                     {"owner",
                      validation::Required{},
                      validation::AccountBase58Validator},
                     {"seq",
                      validation::Required{},
                      validation::Type<uint32_t>{}},
                 },
             }},
            {"offer",
             validation::Type<std::string, boost::json::object>{},
             validation::IfType<std::string>{
                 validation::Uint256HexStringValidator},
             validation::IfType<boost::json::object>{
                 validation::Section{
                     {"account",
                      validation::Required{},
                      validation::AccountBase58Validator},
                     {"seq",
                      validation::Required{},
                      validation::Type<uint32_t>{}},
                 },
             }},
            {"payment_channel", validation::Uint256HexStringValidator},
            {"ripple_state",
             validation::Type<boost::json::object>{},
             validation::Section{
                 {"accounts", validation::Required{}, rippleStateAccountsCheck},
                 {"currency",
                  validation::Required{},
                  validation::CurrencyValidator},
             }},
            {"ticket",
             validation::Type<std::string, boost::json::object>{},
             validation::IfType<std::string>{
                 validation::Uint256HexStringValidator},
             validation::IfType<boost::json::object>{
                 validation::Section{
                     {"account",
                      validation::Required{},
                      validation::AccountBase58Validator},
                     {"ticket_seq",
                      validation::Required{},
                      validation::Type<uint32_t>{}},
                 },
             }},
        };
        return rpcSpec;
    }

    Result
    process(Input input, boost::asio::yield_context& yield) const;

private:
    // dir_root and owner can not be both empty or filled at the same time
    // This function will return an error if this is the case
    std::variant<ripple::uint256, RPC::Status>
    composeKeyFromDirectory(
        boost::json::object const& directory) const noexcept;
};

void
tag_invoke(
    boost::json::value_from_tag,
    boost::json::value& jv,
    LedgerEntryHandler::Output const& output);

LedgerEntryHandler::Input
tag_invoke(
    boost::json::value_to_tag<LedgerEntryHandler::Input>,
    boost::json::value const& jv);
}  // namespace RPCng
