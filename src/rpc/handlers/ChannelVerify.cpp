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

#include <ripple/basics/StringUtilities.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/PayChan.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/jss.h>
#include <ripple/resource/Fees.h>

#include <optional>
#include <rpc/RPCHelpers.h>

namespace RPC {

Result
doChannelVerify(Context const& context)
{
    auto request = context.params;
    boost::json::object response = {};

    if (!request.contains(JS(amount)))
        return Status{RippledError::rpcINVALID_PARAMS, "missingAmount"};

    if (!request.at(JS(amount)).is_string())
        return Status{RippledError::rpcINVALID_PARAMS, "amountNotString"};

    if (!request.contains(JS(signature)))
        return Status{RippledError::rpcINVALID_PARAMS, "missingSignature"};

    if (!request.at(JS(signature)).is_string())
        return Status{RippledError::rpcINVALID_PARAMS, "signatureNotString"};

    if (!request.contains(JS(public_key)))
        return Status{RippledError::rpcINVALID_PARAMS, "missingPublicKey"};

    if (!request.at(JS(public_key)).is_string())
        return Status{RippledError::rpcINVALID_PARAMS, "publicKeyNotString"};

    std::optional<ripple::PublicKey> pk;
    {
        std::string const strPk =
            request.at(JS(public_key)).as_string().c_str();
        pk = ripple::parseBase58<ripple::PublicKey>(
            ripple::TokenType::AccountPublic, strPk);

        if (!pk)
        {
            auto pkHex = ripple::strUnHex(strPk);
            if (!pkHex)
                return Status{
                    RippledError::rpcPUBLIC_MALFORMED, "malformedPublicKey"};

            auto const pkType =
                ripple::publicKeyType(ripple::makeSlice(*pkHex));
            if (!pkType)
                return Status{
                    RippledError::rpcPUBLIC_MALFORMED, "invalidKeyType"};

            pk.emplace(ripple::makeSlice(*pkHex));
        }
    }

    ripple::uint256 channelId;
    if (auto const status = getChannelId(request, channelId); status)
        return status;

    auto optDrops =
        ripple::to_uint64(request.at(JS(amount)).as_string().c_str());

    if (!optDrops)
        return Status{
            RippledError::rpcCHANNEL_AMT_MALFORMED, "couldNotParseAmount"};

    std::uint64_t drops = *optDrops;

    auto sig = ripple::strUnHex(request.at(JS(signature)).as_string().c_str());

    if (!sig || !sig->size())
        return Status{RippledError::rpcINVALID_PARAMS, "invalidSignature"};

    ripple::Serializer msg;
    ripple::serializePayChanAuthorization(
        msg, channelId, ripple::XRPAmount(drops));

    response[JS(signature_verified)] =
        ripple::verify(*pk, msg.slice(), ripple::makeSlice(*sig), true);

    return response;
}

}  // namespace RPC
