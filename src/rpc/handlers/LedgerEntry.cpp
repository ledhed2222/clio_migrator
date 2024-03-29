//------------------------------------------------------------------------------
/*
    This file is part of clio: https://github.com/XRPLF/clio
    Copyright (c) 2022, the clio developers.

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

#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/STLedgerEntry.h>
#include <boost/json.hpp>

#include <backend/BackendInterface.h>
#include <rpc/RPCHelpers.h>
// {
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
//   ...
// }

namespace RPC {

using boost::json::value_to;

Result
doLedgerEntry(Context const& context)
{
    auto request = context.params;
    boost::json::object response = {};

    bool const binary = getBool(request, "binary", false);

    auto v = ledgerInfoFromRequest(context);
    if (auto status = std::get_if<Status>(&v))
        return *status;

    auto lgrInfo = std::get<ripple::LedgerInfo>(v);

    ripple::uint256 key;
    // the expected type of the entry object
    auto expectedType = ripple::ltANY;

    // Note: according to docs, only 1 of the below should be specified at any
    // time. see https://xrpl.org/ledger_entry.html#ledger_entry
    if (request.contains(JS(index)))
    {
        if (!request.at(JS(index)).is_string())
            return Status{RippledError::rpcINVALID_PARAMS, "indexNotString"};

        if (!key.parseHex(request.at(JS(index)).as_string().c_str()))
            return Status{ClioError::rpcMALFORMED_REQUEST};
    }
    else if (request.contains(JS(account_root)))
    {
        if (!request.at(JS(account_root)).is_string())
            return Status{
                RippledError::rpcINVALID_PARAMS, "account_rootNotString"};

        auto const account = ripple::parseBase58<ripple::AccountID>(
            request.at(JS(account_root)).as_string().c_str());
        expectedType = ripple::ltACCOUNT_ROOT;
        if (!account || account->isZero())
            return Status{ClioError::rpcMALFORMED_ADDRESS};
        else
            key = ripple::keylet::account(*account).key;
    }
    else if (request.contains(JS(check)))
    {
        if (!request.at(JS(check)).is_string())
            return Status{RippledError::rpcINVALID_PARAMS, "checkNotString"};

        expectedType = ripple::ltCHECK;
        if (!key.parseHex(request.at(JS(check)).as_string().c_str()))
        {
            return Status{RippledError::rpcINVALID_PARAMS, "checkMalformed"};
        }
    }
    else if (request.contains(JS(deposit_preauth)))
    {
        expectedType = ripple::ltDEPOSIT_PREAUTH;
        if (!request.at(JS(deposit_preauth)).is_object())
        {
            if (!request.at(JS(deposit_preauth)).is_string() ||
                !key.parseHex(
                    request.at(JS(deposit_preauth)).as_string().c_str()))
            {
                return Status{
                    RippledError::rpcINVALID_PARAMS,
                    "deposit_preauthMalformed"};
            }
        }
        else if (
            !request.at(JS(deposit_preauth)).as_object().contains(JS(owner)) ||
            !request.at(JS(deposit_preauth))
                 .as_object()
                 .at(JS(owner))
                 .is_string())
        {
            return Status{RippledError::rpcINVALID_PARAMS, "malformedOwner"};
        }
        else if (
            !request.at(JS(deposit_preauth))
                 .as_object()
                 .contains(JS(authorized)) ||
            !request.at(JS(deposit_preauth))
                 .as_object()
                 .at(JS(authorized))
                 .is_string())
        {
            return Status{
                RippledError::rpcINVALID_PARAMS, "authorizedNotString"};
        }
        else
        {
            boost::json::object const& deposit_preauth =
                request.at(JS(deposit_preauth)).as_object();

            auto const owner = ripple::parseBase58<ripple::AccountID>(
                deposit_preauth.at(JS(owner)).as_string().c_str());

            auto const authorized = ripple::parseBase58<ripple::AccountID>(
                deposit_preauth.at(JS(authorized)).as_string().c_str());

            if (!owner)
                return Status{
                    RippledError::rpcINVALID_PARAMS, "malformedOwner"};
            else if (!authorized)
                return Status{
                    RippledError::rpcINVALID_PARAMS, "malformedAuthorized"};
            else
                key = ripple::keylet::depositPreauth(*owner, *authorized).key;
        }
    }
    else if (request.contains(JS(directory)))
    {
        expectedType = ripple::ltDIR_NODE;
        if (!request.at(JS(directory)).is_object())
        {
            if (!request.at(JS(directory)).is_string())
                return Status{
                    RippledError::rpcINVALID_PARAMS, "directoryNotString"};

            if (!key.parseHex(request.at(JS(directory)).as_string().c_str()))
            {
                return Status{
                    RippledError::rpcINVALID_PARAMS, "malformedDirectory"};
            }
        }
        else if (
            request.at(JS(directory)).as_object().contains(JS(sub_index)) &&
            !request.at(JS(directory)).as_object().at(JS(sub_index)).is_int64())
        {
            return Status{RippledError::rpcINVALID_PARAMS, "sub_indexNotInt"};
        }
        else
        {
            auto directory = request.at(JS(directory)).as_object();
            std::uint64_t subIndex = directory.contains(JS(sub_index))
                ? boost::json::value_to<std::uint64_t>(
                      directory.at(JS(sub_index)))
                : 0;

            if (directory.contains(JS(dir_root)))
            {
                ripple::uint256 uDirRoot;

                if (directory.contains(JS(owner)))
                {
                    // May not specify both dir_root and owner.
                    return Status{
                        RippledError::rpcINVALID_PARAMS,
                        "mayNotSpecifyBothDirRootAndOwner"};
                }
                else if (!uDirRoot.parseHex(
                             directory.at(JS(dir_root)).as_string().c_str()))
                {
                    return Status{
                        RippledError::rpcINVALID_PARAMS, "malformedDirRoot"};
                }
                else
                {
                    key = ripple::keylet::page(uDirRoot, subIndex).key;
                }
            }
            else if (directory.contains(JS(owner)))
            {
                auto const ownerID = ripple::parseBase58<ripple::AccountID>(
                    directory.at(JS(owner)).as_string().c_str());

                if (!ownerID)
                {
                    return Status{ClioError::rpcMALFORMED_ADDRESS};
                }
                else
                {
                    key = ripple::keylet::page(
                              ripple::keylet::ownerDir(*ownerID), subIndex)
                              .key;
                }
            }
            else
            {
                return Status{
                    RippledError::rpcINVALID_PARAMS, "missingOwnerOrDirRoot"};
            }
        }
    }
    else if (request.contains(JS(escrow)))
    {
        expectedType = ripple::ltESCROW;
        if (!request.at(JS(escrow)).is_object())
        {
            if (!key.parseHex(request.at(JS(escrow)).as_string().c_str()))
                return Status{
                    RippledError::rpcINVALID_PARAMS, "malformedEscrow"};
        }
        else if (
            !request.at(JS(escrow)).as_object().contains(JS(owner)) ||
            !request.at(JS(escrow)).as_object().at(JS(owner)).is_string())
        {
            return Status{RippledError::rpcINVALID_PARAMS, "malformedOwner"};
        }
        else if (
            !request.at(JS(escrow)).as_object().contains(JS(seq)) ||
            !request.at(JS(escrow)).as_object().at(JS(seq)).is_int64())
        {
            return Status{RippledError::rpcINVALID_PARAMS, "malformedSeq"};
        }
        else
        {
            auto const id =
                ripple::parseBase58<ripple::AccountID>(request.at(JS(escrow))
                                                           .as_object()
                                                           .at(JS(owner))
                                                           .as_string()
                                                           .c_str());

            if (!id)
                return Status{ClioError::rpcMALFORMED_ADDRESS};
            else
            {
                std::uint32_t seq =
                    request.at(JS(escrow)).as_object().at(JS(seq)).as_int64();
                key = ripple::keylet::escrow(*id, seq).key;
            }
        }
    }
    else if (request.contains(JS(offer)))
    {
        expectedType = ripple::ltOFFER;
        if (!request.at(JS(offer)).is_object())
        {
            if (!key.parseHex(request.at(JS(offer)).as_string().c_str()))
                return Status{
                    RippledError::rpcINVALID_PARAMS, "malformedOffer"};
        }
        else if (
            !request.at(JS(offer)).as_object().contains(JS(account)) ||
            !request.at(JS(offer)).as_object().at(JS(account)).is_string())
        {
            return Status{RippledError::rpcINVALID_PARAMS, "malformedAccount"};
        }
        else if (
            !request.at(JS(offer)).as_object().contains(JS(seq)) ||
            !request.at(JS(offer)).as_object().at(JS(seq)).is_int64())
        {
            return Status{RippledError::rpcINVALID_PARAMS, "malformedSeq"};
        }
        else
        {
            auto offer = request.at(JS(offer)).as_object();
            auto const id = ripple::parseBase58<ripple::AccountID>(
                offer.at(JS(account)).as_string().c_str());

            if (!id)
                return Status{ClioError::rpcMALFORMED_ADDRESS};
            else
            {
                std::uint32_t seq =
                    boost::json::value_to<std::uint32_t>(offer.at(JS(seq)));
                key = ripple::keylet::offer(*id, seq).key;
            }
        }
    }
    else if (request.contains(JS(payment_channel)))
    {
        expectedType = ripple::ltPAYCHAN;
        if (!request.at(JS(payment_channel)).is_string())
            return Status{
                RippledError::rpcINVALID_PARAMS, "paymentChannelNotString"};

        if (!key.parseHex(request.at(JS(payment_channel)).as_string().c_str()))
            return Status{
                RippledError::rpcINVALID_PARAMS, "malformedPaymentChannel"};
    }
    else if (request.contains(JS(ripple_state)))
    {
        if (!request.at(JS(ripple_state)).is_object())
            return Status{
                RippledError::rpcINVALID_PARAMS, "rippleStateNotObject"};

        expectedType = ripple::ltRIPPLE_STATE;
        ripple::Currency currency;
        boost::json::object const& state =
            request.at(JS(ripple_state)).as_object();

        if (!state.contains(JS(currency)) ||
            !state.at(JS(currency)).is_string())
        {
            return Status{RippledError::rpcINVALID_PARAMS, "currencyNotString"};
        }

        if (!state.contains(JS(accounts)) ||
            !state.at(JS(accounts)).is_array() ||
            2 != state.at(JS(accounts)).as_array().size() ||
            !state.at(JS(accounts)).as_array().at(0).is_string() ||
            !state.at(JS(accounts)).as_array().at(1).is_string() ||
            (state.at(JS(accounts)).as_array().at(0).as_string() ==
             state.at(JS(accounts)).as_array().at(1).as_string()))
        {
            return Status{RippledError::rpcINVALID_PARAMS, "malformedAccounts"};
        }

        auto const id1 = ripple::parseBase58<ripple::AccountID>(
            state.at(JS(accounts)).as_array().at(0).as_string().c_str());
        auto const id2 = ripple::parseBase58<ripple::AccountID>(
            state.at(JS(accounts)).as_array().at(1).as_string().c_str());

        if (!id1 || !id2)
            return Status{
                ClioError::rpcMALFORMED_ADDRESS, "malformedAddresses"};

        else if (!ripple::to_currency(
                     currency, state.at(JS(currency)).as_string().c_str()))
            return Status{
                ClioError::rpcMALFORMED_CURRENCY, "malformedCurrency"};

        key = ripple::keylet::line(*id1, *id2, currency).key;
    }
    else if (request.contains(JS(ticket)))
    {
        expectedType = ripple::ltTICKET;
        // ticket object : account, ticket_seq
        if (!request.at(JS(ticket)).is_object())
        {
            if (!request.at(JS(ticket)).is_string())
                return Status{
                    ClioError::rpcMALFORMED_REQUEST, "ticketNotString"};

            if (!key.parseHex(request.at(JS(ticket)).as_string().c_str()))
                return Status{
                    ClioError::rpcMALFORMED_REQUEST, "malformedTicket"};
        }
        else if (
            !request.at(JS(ticket)).as_object().contains(JS(account)) ||
            !request.at(JS(ticket)).as_object().at(JS(account)).is_string())
        {
            return Status{ClioError::rpcMALFORMED_REQUEST};
        }
        else if (
            !request.at(JS(ticket)).as_object().contains(JS(ticket_seq)) ||
            !request.at(JS(ticket)).as_object().at(JS(ticket_seq)).is_int64())
        {
            return Status{
                ClioError::rpcMALFORMED_REQUEST, "malformedTicketSeq"};
        }
        else
        {
            auto const id =
                ripple::parseBase58<ripple::AccountID>(request.at(JS(ticket))
                                                           .as_object()
                                                           .at(JS(account))
                                                           .as_string()
                                                           .c_str());

            if (!id)
                return Status{ClioError::rpcMALFORMED_OWNER};
            else
            {
                std::uint32_t seq = request.at(JS(ticket))
                                        .as_object()
                                        .at(JS(ticket_seq))
                                        .as_int64();

                key = ripple::getTicketIndex(*id, seq);
            }
        }
    }
    else
    {
        return Status{RippledError::rpcINVALID_PARAMS, "unknownOption"};
    }

    auto dbResponse =
        context.backend->fetchLedgerObject(key, lgrInfo.seq, context.yield);

    if (!dbResponse or dbResponse->size() == 0)
        return Status{"entryNotFound"};

    // check expected type matches actual type
    ripple::STLedgerEntry sle{
        ripple::SerialIter{dbResponse->data(), dbResponse->size()}, key};
    if (expectedType != ripple::ltANY && sle.getType() != expectedType)
        return Status{"unexpectedLedgerType"};

    response[JS(index)] = ripple::strHex(key);
    response[JS(ledger_hash)] = ripple::strHex(lgrInfo.hash);
    response[JS(ledger_index)] = lgrInfo.seq;

    if (binary)
    {
        response[JS(node_binary)] = ripple::strHex(*dbResponse);
    }
    else
    {
        response[JS(node)] = toJson(sle);
    }

    return response;
}

}  // namespace RPC
