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

#include <rpc/RPCHelpers.h>
#include <rpc/ngHandlers/LedgerEntry.h>

#include <unordered_map>

namespace RPCng {
LedgerEntryHandler::Result
LedgerEntryHandler::process(
    LedgerEntryHandler::Input input,
    boost::asio::yield_context& yield) const
{
    ripple::uint256 key;
    if (input.index)
    {
        key = ripple::uint256{std::string_view(*(input.index))};
    }
    else if (input.accountRoot)
    {
        key = ripple::keylet::account(
                  *ripple::parseBase58<ripple::AccountID>(*(input.accountRoot)))
                  .key;
    }
    else if (input.directory)
    {
        auto const keyOrStatus = composeKeyFromDirectory(*input.directory);
        if (auto const status = std::get_if<RPC::Status>(&keyOrStatus))
            return Error{*status};
        key = std::get<ripple::uint256>(keyOrStatus);
    }
    else if (input.offer)
    {
        auto const id = ripple::parseBase58<ripple::AccountID>(
            input.offer->at("account").as_string().c_str());
        key = ripple::keylet::offer(
                  *id,
                  boost::json::value_to<std::uint32_t>(input.offer->at("seq")))
                  .key;
    }
    else if (input.rippleStateAccount)
    {
        auto const id1 = ripple::parseBase58<ripple::AccountID>(
            input.rippleStateAccount->at("accounts")
                .as_array()
                .at(0)
                .as_string()
                .c_str());
        auto const id2 = ripple::parseBase58<ripple::AccountID>(
            input.rippleStateAccount->at("accounts")
                .as_array()
                .at(1)
                .as_string()
                .c_str());
        auto const currency = ripple::to_currency(
            input.rippleStateAccount->at("currency").as_string().c_str());
        key = ripple::keylet::line(*id1, *id2, currency).key;
    }
    else if (input.escrow)
    {
        auto const id = ripple::parseBase58<ripple::AccountID>(
            input.escrow->at("owner").as_string().c_str());
        key =
            ripple::keylet::escrow(*id, input.escrow->at("seq").as_int64()).key;
    }
    else if (input.depositPreauth)
    {
        auto const owner = ripple::parseBase58<ripple::AccountID>(
            input.depositPreauth->at("owner").as_string().c_str());
        auto const authorized = ripple::parseBase58<ripple::AccountID>(
            input.depositPreauth->at("authorized").as_string().c_str());
        key = ripple::keylet::depositPreauth(*owner, *authorized).key;
    }
    else if (input.ticket)
    {
        auto const id = ripple::parseBase58<ripple::AccountID>(
            input.ticket->at("account").as_string().c_str());
        key = ripple::getTicketIndex(
            *id, input.ticket->at("ticket_seq").as_int64());
    }
    else
    {
        // Must specify 1 of the following fields to indicate what type
        return Error{
            RPC::Status{RPC::RippledError::rpcINVALID_PARAMS, "unknownOption"}};
    }

    // check ledger exists
    auto const range = sharedPtrBackend_->fetchLedgerRange();
    auto const lgrInfoOrStatus = RPC::getLedgerInfoFromHashOrSeq(
        *sharedPtrBackend_,
        yield,
        input.ledgerHash,
        input.ledgerIndex,
        range->maxSequence);

    if (auto const status = std::get_if<RPC::Status>(&lgrInfoOrStatus))
        return Error{*status};

    auto const lgrInfo = std::get<ripple::LedgerInfo>(lgrInfoOrStatus);
    auto const ledgerObject =
        sharedPtrBackend_->fetchLedgerObject(key, lgrInfo.seq, yield);
    if (!ledgerObject || ledgerObject->size() == 0)
        return Error{RPC::Status{"entryNotFound"}};

    ripple::STLedgerEntry const sle{
        ripple::SerialIter{ledgerObject->data(), ledgerObject->size()}, key};
    if (input.expectedType != ripple::ltANY &&
        sle.getType() != input.expectedType)
        return Error{RPC::Status{"unexpectedLedgerType"}};

    LedgerEntryHandler::Output output;
    output.index = ripple::strHex(key);
    output.ledgerIndex = lgrInfo.seq;
    output.ledgerHash = ripple::strHex(lgrInfo.hash);
    if (input.binary)
    {
        output.nodeBinary = ripple::strHex(*ledgerObject);
    }
    else
    {
        output.node = RPC::toJson(sle);
    }
    return output;
}

std::variant<ripple::uint256, RPC::Status>
LedgerEntryHandler::composeKeyFromDirectory(
    boost::json::object const& directory) const noexcept
{
    // can not specify both dir_root and owner.
    if (directory.contains("dir_root") && directory.contains("owner"))
        return RPC::Status{
            RPC::RippledError::rpcINVALID_PARAMS,
            "mayNotSpecifyBothDirRootAndOwner"};
    // at least one should availiable
    if (!(directory.contains("dir_root") || directory.contains("owner")))
        return RPC::Status{
            RPC::RippledError::rpcINVALID_PARAMS, "missingOwnerOrDirRoot"};

    uint64_t const subIndex = directory.contains("sub_index")
        ? boost::json::value_to<uint64_t>(directory.at("sub_index"))
        : 0;

    if (directory.contains("dir_root"))
    {
        ripple::uint256 const uDirRoot{
            directory.at("dir_root").as_string().c_str()};
        return ripple::keylet::page(uDirRoot, subIndex).key;
    }

    auto const ownerID = ripple::parseBase58<ripple::AccountID>(
        directory.at("owner").as_string().c_str());
    return ripple::keylet::page(ripple::keylet::ownerDir(*ownerID), subIndex)
        .key;
}

void
tag_invoke(
    boost::json::value_from_tag,
    boost::json::value& jv,
    LedgerEntryHandler::Output const& output)
{
    auto object = boost::json::object{
        {"ledger_hash", output.ledgerHash},
        {"ledger_index", output.ledgerIndex},
        {"validated", output.validated},
        {"index", output.index}};
    if (output.nodeBinary)
    {
        object["node_binary"] = *(output.nodeBinary);
    }
    else
    {
        object["node"] = *(output.node);
    }
    jv = std::move(object);
}

LedgerEntryHandler::Input
tag_invoke(
    boost::json::value_to_tag<LedgerEntryHandler::Input>,
    boost::json::value const& jv)
{
    auto const& jsonObject = jv.as_object();
    LedgerEntryHandler::Input input;
    if (jsonObject.contains("ledger_hash"))
    {
        input.ledgerHash = jv.at("ledger_hash").as_string().c_str();
    }
    if (jsonObject.contains("ledger_index"))
    {
        if (!jsonObject.at("ledger_index").is_string())
        {
            input.ledgerIndex = jv.at("ledger_index").as_int64();
        }
        else if (jsonObject.at("ledger_index").as_string() != "validated")
        {
            input.ledgerIndex =
                std::stoi(jv.at("ledger_index").as_string().c_str());
        }
    }
    if (jsonObject.contains("binary"))
    {
        input.binary = jv.at("binary").as_bool();
    }
    // check all the protential index
    static auto const indexFieldTypeMap =
        std::unordered_map<std::string, ripple::LedgerEntryType>{
            {"index", ripple::ltANY},
            {"directory", ripple::ltDIR_NODE},
            {"offer", ripple::ltOFFER},
            {"check", ripple::ltCHECK},
            {"escrow", ripple::ltESCROW},
            {"payment_channel", ripple::ltPAYCHAN},
            {"deposit_preauth", ripple::ltDEPOSIT_PREAUTH},
            {"ticket", ripple::ltTICKET}};

    auto const indexFieldType = std::find_if(
        indexFieldTypeMap.begin(),
        indexFieldTypeMap.end(),
        [&jsonObject](auto const& pair) {
            auto const& [field, _] = pair;
            return jsonObject.contains(field) &&
                jsonObject.at(field).is_string();
        });
    if (indexFieldType != indexFieldTypeMap.end())
    {
        input.index = jv.at(indexFieldType->first).as_string().c_str();
        input.expectedType = indexFieldType->second;
    }
    // check if request for account root
    else if (jsonObject.contains("account_root"))
    {
        input.accountRoot = jv.at("account_root").as_string().c_str();
    }
    // no need to check if_object again, validator only allows string or object
    else if (jsonObject.contains("directory"))
    {
        input.directory = jv.at("directory").as_object();
    }
    else if (jsonObject.contains("offer"))
    {
        input.offer = jv.at("offer").as_object();
    }
    else if (jsonObject.contains("ripple_state"))
    {
        input.rippleStateAccount = jv.at("ripple_state").as_object();
    }
    else if (jsonObject.contains("escrow"))
    {
        input.escrow = jv.at("escrow").as_object();
    }
    else if (jsonObject.contains("deposit_preauth"))
    {
        input.depositPreauth = jv.at("deposit_preauth").as_object();
    }
    else if (jsonObject.contains("ticket"))
    {
        input.ticket = jv.at("ticket").as_object();
    }
    return input;
}

}  // namespace RPCng
