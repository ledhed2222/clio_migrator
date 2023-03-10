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

#pragma once

#include <rpc/RPC.h>

namespace RPC {
/*
 * This file just contains declarations for all of the handlers
 */

// account state methods
Result
doAccountInfo(Context const& context);

Result
doAccountChannels(Context const& context);

Result
doAccountCurrencies(Context const& context);

Result
doAccountLines(Context const& context);

Result
doAccountNFTs(Context const& context);

Result
doAccountObjects(Context const& context);

Result
doAccountOffers(Context const& context);

Result
doGatewayBalances(Context const& context);

Result
doNoRippleCheck(Context const& context);

// channels methods

Result
doChannelAuthorize(Context const& context);

Result
doChannelVerify(Context const& context);

// book methods
[[nodiscard]] Result
doBookChanges(Context const& context);

Result
doBookOffers(Context const& context);

// NFT methods
Result
doNFTBuyOffers(Context const& context);

Result
doNFTSellOffers(Context const& context);

Result
doNFTInfo(Context const& context);

Result
doNFTHistory(Context const& context);

// ledger methods
Result
doLedger(Context const& context);

Result
doLedgerEntry(Context const& context);

Result
doLedgerData(Context const& context);

Result
doLedgerRange(Context const& context);

// transaction methods
Result
doTx(Context const& context);

Result
doTransactionEntry(Context const& context);

Result
doAccountTx(Context const& context);

// subscriptions
Result
doSubscribe(Context const& context);

Result
doUnsubscribe(Context const& context);

// server methods
Result
doServerInfo(Context const& context);

// Utility methods
Result
doRandom(Context const& context);
}  // namespace RPC
