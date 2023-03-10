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

void
serializePayChanAuthorization(
    ripple::Serializer& msg,
    ripple::uint256 const& key,
    ripple::XRPAmount const& amt)
{
    msg.add32(ripple::HashPrefix::paymentChannelClaim);
    msg.addBitString(key);
    msg.add64(amt.drops());
}

Result
doChannelAuthorize(Context const& context)
{
    auto request = context.params;
    boost::json::object response = {};

    if (!request.contains(JS(amount)))
        return Status{RippledError::rpcINVALID_PARAMS, "missingAmount"};

    if (!request.at(JS(amount)).is_string())
        return Status{RippledError::rpcINVALID_PARAMS, "amountNotString"};

    if (!request.contains(JS(key_type)) && !request.contains(JS(secret)))
        return Status{
            RippledError::rpcINVALID_PARAMS, "missingKeyTypeOrSecret"};

    auto v = keypairFromRequst(request);
    if (auto status = std::get_if<Status>(&v))
        return *status;

    auto const [pk, sk] =
        std::get<std::pair<ripple::PublicKey, ripple::SecretKey>>(v);

    ripple::uint256 channelId;
    if (auto const status = getChannelId(request, channelId); status)
        return status;

    auto optDrops =
        ripple::to_uint64(request.at(JS(amount)).as_string().c_str());

    if (!optDrops)
        return Status{
            RippledError::rpcCHANNEL_AMT_MALFORMED, "couldNotParseAmount"};

    std::uint64_t drops = *optDrops;

    ripple::Serializer msg;
    ripple::serializePayChanAuthorization(
        msg, channelId, ripple::XRPAmount(drops));

    try
    {
        auto const buf = ripple::sign(pk, sk, msg.slice());
        response[JS(signature)] = ripple::strHex(buf);
    }
    catch (std::exception&)
    {
        return Status{RippledError::rpcINTERNAL};
    }

    return response;
}

}  // namespace RPC
