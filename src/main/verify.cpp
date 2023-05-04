#include <backend/BackendFactory.h>
#include <backend/CassandraBackend.h>
#include <config/Config.h>
#include <etl/NFTHelpers.h>
#include <main/Build.h>

#include <rpc/Errors.h>
#include <boost/asio.hpp>
#include <boost/log/trivial.hpp>
#include <cassandra.h>

#include <iostream>

static std::uint32_t const MAX_RETRIES = 5;
static std::chrono::seconds const WAIT_TIME = std::chrono::seconds(60);
using Blob = std::vector<unsigned char>;

static void
wait(boost::asio::steady_timer& timer, std::string const reason)
{
    BOOST_LOG_TRIVIAL(info) << reason << ". Waiting";
    timer.expires_after(WAIT_TIME);
    timer.wait();
    BOOST_LOG_TRIVIAL(info) << "Done";
}


static std::optional<Backend::TransactionAndMetadata>
doTryFetchTransaction(
    boost::asio::steady_timer& timer,
    Backend::CassandraBackend& backend,
    ripple::uint256 const& hash,
    boost::asio::yield_context& yield,
    std::uint32_t const attempts = 0)
{
    try
    {
        return backend.fetchTransaction(hash, yield);
    }
    catch (Backend::DatabaseTimeout const& e)
    {
        if (attempts >= MAX_RETRIES)
            throw e;

        wait(timer, "Transaction read error");
        return doTryFetchTransaction(timer, backend, hash, yield, attempts + 1);
    }
}

static Backend::LedgerPage
doTryFetchLedgerPage(
    boost::asio::steady_timer& timer,
    Backend::CassandraBackend& backend,
    std::optional<ripple::uint256> const& cursor,
    std::uint32_t const sequence,
    boost::asio::yield_context& yield,
    std::uint32_t const attempts = 0)
{
    try
    {
        return backend.fetchLedgerPage(cursor, sequence, 2000, false, yield);
    }
    catch (Backend::DatabaseTimeout const& e)
    {
        if (attempts >= MAX_RETRIES)
            throw e;

        wait(timer, "Page read error");
        return doTryFetchLedgerPage(
            timer, backend, cursor, sequence, yield, attempts + 1);
    }
}

//TODO change this function
static const CassResult*
doTryGetTxPageResult(
    CassStatement* const query,
    boost::asio::steady_timer& timer,
    Backend::CassandraBackend& backend,
    std::uint32_t const attempts = 0)
{
    CassFuture* fut = cass_session_execute(backend.cautionGetSession(), query);
    CassResult const* result = cass_future_get_result(fut);
    cass_future_free(fut);

    if (result != nullptr)
        return result;

    if (attempts >= MAX_RETRIES)
        throw std::runtime_error("Already retried too many times");

    wait(timer, "Unexpected empty result from nft paging");
    return doTryGetTxPageResult(query, timer, backend, attempts + 1);
}

static void
verifyNFTs(
    std::vector<NFTsData>& nfts,
    Backend::CassandraBackend& backend,
    boost::asio::yield_context& yield)
{
    if (nfts.size() <= 0)
        return;

    for(auto const& nft: nfts){
        std::optional<Backend::NFT> writtenNFT = backend.fetchNFT(nft.tokenID, nft.ledgerSequence, yield);

        if(!writtenNFT.has_value())
            throw std::runtime_error("NFT was not written!");

        Blob writtenUriBlob = writtenNFT->uri;
        std::string writtenUriStr = ripple::strHex(writtenUriBlob);

        auto fetchOldUri = nft.uri;
        std::string oldUriStr = nft.uri.has_value() ? ripple::strHex(nft.uri.value()) : "";

        if(oldUriStr.compare(writtenUriStr) != 0){
            BOOST_LOG_TRIVIAL(warning) <<"\nNFTokenID "<< to_string(nft.tokenID) << " failed to match URIs!\n";  
            throw std::runtime_error("Failed to match!");
        }
        else{
            BOOST_LOG_TRIVIAL(info) <<"\nNFTokenID "<< to_string(nft.tokenID) << " URI matched!\n";         
        }
    }

}

static void
doVerification(
    Backend::CassandraBackend& backend,
    boost::asio::steady_timer& timer,
    boost::asio::yield_context& yield)
{
    BOOST_LOG_TRIVIAL(info) << "Beginning verification";
    auto const ledgerRange = backend.hardFetchLedgerRangeNoThrow(yield);

    /*
     * Step 0 - If we haven't downloaded the initial ledger yet, just short
     * circuit.
     */
    if (!ledgerRange)
    {
        BOOST_LOG_TRIVIAL(info) << "There is no data to verification";
        return;
    }

    /*
     * Step 1 - Look at all NFT transactions recorded in
     * `nf_token_transactions` and reload any NFTokenMint transactions. These
     * will contain the URI of any tokens that were minted after our start
     * sequence. We look at transactions for this step instead of directly at
     * the tokens in `nf_tokens` because we also want to cover the extreme
     * edge case of a token that is re-minted with a different URI.
     */
    std::stringstream query;
    std::vector<std::uint32_t> ledgerSequencesChanged;
    query << "SELECT sequence FROM " << backend.tablePrefix()
          << "nf_token_uris" ; // may need to run unique
    CassStatement* nftTxQuery = cass_statement_new(query.str().c_str(), 0);
    cass_statement_set_paging_size(nftTxQuery, 1000);
    cass_bool_t morePages = cass_true;

    // For all NFT txs, paginated in groups of 1000...
    while (morePages)
    {
        std::vector<std::uint32_t> ledgerSequencePage;

        CassResult const* result =
            doTryGetTxPageResult(nftTxQuery, timer, backend);

        // For each tx in page...
        CassIterator* txPageIterator = cass_iterator_from_result(result);
        while (cass_iterator_next(txPageIterator))
        {
            cass_int64_t buf;

            CassError rc = cass_value_get_int64(cass_row_get_column(cass_iterator_get_row(txPageIterator), 0), &buf);
            
            if (rc != CASS_OK)
            {
                cass_iterator_free(txPageIterator);
                cass_result_free(result);
                cass_statement_free(nftTxQuery);
                throw std::runtime_error(
                    "Could not retrieve hash from nf_token_transactions");
            }

            std::uint32_t seq = static_cast<std::uint32_t>(buf);
            if(seq <= ledgerRange->maxSequence)
                ledgerSequencePage.push_back(seq);
        }

        // make ledgerSequencePage unique
        sort(ledgerSequencePage.begin(), ledgerSequencePage.end());
        ledgerSequencePage.erase(unique(ledgerSequencePage.begin(), ledgerSequencePage.end()), ledgerSequencePage.end());
        ledgerSequencesChanged.insert(ledgerSequencesChanged.end(), ledgerSequencePage.begin(), ledgerSequencePage.end());

        morePages = cass_result_has_more_pages(result);
        if (morePages)
            cass_statement_set_paging_state(nftTxQuery, result);
        cass_iterator_free(txPageIterator);
        cass_result_free(result);
    }

    //unique the ledgerSequencesChanged vector
    sort(ledgerSequencesChanged.begin(), ledgerSequencesChanged.end());
    ledgerSequencesChanged.erase(unique(ledgerSequencesChanged.begin(), ledgerSequencesChanged.end()), ledgerSequencesChanged.end());

    cass_statement_free(nftTxQuery);
    BOOST_LOG_TRIVIAL(info) << "\nDone with querying ledger sequences!\n";

    /*
     * Step 2 - Pull every object from our initial ledger and load all NFTs
     * found in any NFTokenPage object. Prior to this migration, we were not
     * pulling out NFTs from the initial ledger, so all these NFTs would be
     * missed. This will also record the URI of any NFTs minted prior to the
     * start sequence.
     */


    for(auto const ledgerSeq: ledgerSequencesChanged){
        std::optional<ripple::uint256> cursor;

        do
        {
            auto const page = doTryFetchLedgerPage(
                timer, backend, cursor, ledgerSeq, yield);
            for (auto const& object : page.objects)
            {
                std::vector<NFTsData> toVerify = getNFTDataFromObj(
                    ledgerSeq,
                    ripple::to_string(object.key),
                    std::string(object.blob.begin(), object.blob.end()));

                //helper function to verify vector of NFTs
                verifyNFTs(toVerify, backend, yield);
            }
            cursor = page.cursor;
        } while (cursor.has_value());
    }
    BOOST_LOG_TRIVIAL(info) << "\nDone with migration verification!\n";
}

int
main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cerr << "Didn't provide config path!" << std::endl;
        return EXIT_FAILURE;
    }

    std::string const configPath = argv[1];
    auto const config = clio::ConfigReader::open(configPath);
    if (!config)
    {
        std::cerr << "Couldn't parse config '" << configPath << "'"
                  << std::endl;
        return EXIT_FAILURE;
    }

    auto const type = config.value<std::string>("database.type");
    if (!boost::iequals(type, "cassandra"))
    {
        std::cerr << "Migration only for cassandra dbs" << std::endl;
        return EXIT_FAILURE;
    }

    boost::asio::io_context ioc;
    boost::asio::steady_timer timer{ioc};
    auto workGuard = boost::asio::make_work_guard(ioc);
    auto backend = Backend::make_Backend(ioc, config);

    boost::asio::spawn(
        ioc, [&backend, &workGuard, &timer](boost::asio::yield_context yield) {
            //TODO: add ledger seq
            doVerification(*backend, timer, yield);
            workGuard.reset();
        });

    ioc.run();
    BOOST_LOG_TRIVIAL(info) << "SUCCESS!";
    return EXIT_SUCCESS;
}