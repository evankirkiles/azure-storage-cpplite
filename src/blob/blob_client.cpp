#include <algorithm>
#include <sstream>

#ifdef _WIN32
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#endif

#include "blob/blob_client.h"

#include "blob/download_blob_request.h"
#include "blob/create_block_blob_request.h"
#include "blob/delete_blob_request.h"
#include "blob/copy_blob_request.h"
#include "blob/create_container_request.h"
#include "blob/delete_container_request.h"
#include "blob/list_containers_request.h"
#include "blob/list_blobs_request.h"
#include "blob/get_block_list_request.h"
#include "blob/get_blob_property_request.h"
#include "blob/get_container_property_request.h"
#include "blob/put_block_request.h"
#include "blob/put_block_list_request.h"
#include "blob/append_block_request.h"
#include "blob/put_page_request.h"
#include "blob/get_page_ranges_request.h"

#include "executor.h"
#include "utility.h"
#include "tinyxml2_parser.h"
#include <curl/curl.h>

namespace azure {  namespace storage_lite {

namespace {

// Return content size from content-range header or -1 if cannot be obtained.
ssize_t get_length_from_content_range(const std::string &header)
{
   const auto pos = header.rfind('/');
   if (std::string::npos == pos) {
      return -1;
   }
   const auto lengthStr = header.substr(pos + 1);
   ssize_t result;
   if (!(std::istringstream(lengthStr) >> result)) {
      return -1;
   }
   return result;
}

} // noname namespace

storage_outcome<chunk_property> blob_client::get_chunk_to_stream_sync(const std::string &container, const std::string &blob, unsigned long long offset, unsigned long long size, std::ostream &os)
{
    auto http = m_client->get_handle();
    auto request = std::make_shared<download_blob_request>(container, blob);
    if (size > 0) {
        request->set_start_byte(offset);
        request->set_end_byte(offset + size - 1);
    }
    else {
        request->set_start_byte(offset);
    }

    http->set_output_stream(storage_ostream(os));

    // TODO: async submit transfered to sync operation. This can be utilized.
    const auto response = async_executor<void>::submit(m_account, request, http, m_context).get();
    if (response.success())
    {
        chunk_property property{};
        property.etag = http->get_header(constants::header_etag);
        property.totalSize = get_length_from_content_range(http->get_header(constants::header_content_range));
        std::istringstream(http->get_header(constants::header_content_length)) >> property.size;
        property.last_modified = curl_getdate(http->get_header(constants::header_last_modified).c_str(), NULL);
        return storage_outcome<chunk_property>(property);
    }
    return storage_outcome<chunk_property>(storage_error(response.error()));
}

// RESCALE REIMPLEMNTATION FUNCTION
// Uses a cancellation token in the get_chunk_to_stream_sync function and sets the sas_token separately from credentials
storage_outcome<chunk_property> blob_client::get_chunk_to_stream_sync_ctoken(const std::string &container, const std::string &blob, unsigned long long offset, unsigned long long size, std::ostream &os, const std::string& sas_token, const std::atomic<bool>* token, void* cancelFunc)
{
    auto http = m_client->get_handle();
    http->set_progress_function_and_arg(cancelFunc, (void*)token);
    http->set_sas_token(sas_token);

    auto request = std::make_shared<download_blob_request>(container, blob);
    if (size > 0) {
        request->set_start_byte(offset);
        request->set_end_byte(offset + size - 1);
    }
    else {
        request->set_start_byte(offset);
    }

    http->set_output_stream(storage_ostream(os));

    // TODO: async submit transfered to sync operation. This can be utilized.
    const auto response = async_executor<void>::submit(m_account, request, http, m_context).get();
    if (response.success())
    {
        chunk_property property{};
        property.etag = http->get_header(constants::header_etag);
        property.totalSize = get_length_from_content_range(http->get_header(constants::header_content_range));
        std::istringstream(http->get_header(constants::header_content_length)) >> property.size;
        property.last_modified = curl_getdate(http->get_header(constants::header_last_modified).c_str(), NULL);
        return storage_outcome<chunk_property>(property);
    }
    return storage_outcome<chunk_property>(storage_error(response.error()));
}

std::future<storage_outcome<void>> blob_client::download_blob_to_stream(const std::string &container, const std::string &blob, unsigned long long offset, unsigned long long size, std::ostream &os)
{
    auto http = m_client->get_handle();

    auto request = std::make_shared<download_blob_request>(container, blob);

    if (size > 0) {
        request->set_start_byte(offset);
        request->set_end_byte(offset + size - 1);
    }
    else {
        request->set_start_byte(offset);
    }

    http->set_output_stream(storage_ostream(os));

    return async_executor<void>::submit(m_account, request, http, m_context);
}

std::future<storage_outcome<void>> blob_client::upload_block_blob_from_stream(const std::string &container, const std::string &blob, std::istream &is, const std::vector<std::pair<std::string, std::string>> &metadata)
{
    auto http = m_client->get_handle();

    auto request = std::make_shared<create_block_blob_request>(container, blob);

    auto cur = is.tellg();
    is.seekg(0, std::ios_base::end);
    auto end = is.tellg();
    is.seekg(cur);
    request->set_content_length(static_cast<unsigned int>(end - cur));
    if (metadata.size() > 0)
    {
        request->set_metadata(metadata);
    }

    http->set_input_stream(storage_istream(is));

    return async_executor<void>::submit(m_account, request, http, m_context);
}

std::future<storage_outcome<void>> blob_client::upload_block_blob_from_stream(const std::string &container, const std::string &blob, std::istream &is, const std::vector<std::pair<std::string, std::string>> &metadata, size_t streamlen)
{
    auto http = m_client->get_handle();

    auto request = std::make_shared<create_block_blob_request>(container, blob);

    request->set_content_length(streamlen);
    if (metadata.size() > 0)
    {
        request->set_metadata(metadata);
    }

    http->set_input_stream(storage_istream(is));
    http->set_is_stream_length();
    http->set_input_stream_length(streamlen);

    return async_executor<void>::submit(m_account, request, http, m_context);
}

std::future<storage_outcome<void>> blob_client::delete_blob(const std::string &container, const std::string &blob, bool delete_snapshots)
{
    auto http = m_client->get_handle();

    auto request = std::make_shared<delete_blob_request>(container, blob, delete_snapshots);

    return async_executor<void>::submit(m_account, request, http, m_context);
}

std::future<storage_outcome<void>> blob_client::create_container(const std::string &container)
{
    auto http = m_client->get_handle();

    auto request = std::make_shared<create_container_request>(container);

    return async_executor<void>::submit(m_account, request, http, m_context);
}

std::future<storage_outcome<void>> blob_client::delete_container(const std::string &container)
{
    auto http = m_client->get_handle();

    auto request = std::make_shared<delete_container_request>(container);

    return async_executor<void>::submit(m_account, request, http, m_context);
}

storage_outcome<container_property> blob_client::get_container_property(const std::string &container)
{
    auto http = m_client->get_handle();

    auto request = std::make_shared<get_container_property_request>(container);

    // TODO: async submit transfered to sync operation. This can be utilized.
    auto response = async_executor<void>::submit(m_account, request, http, m_context).get();
    container_property containerProperty(true);
    if (response.success())
    {
        containerProperty.etag = http->get_header(constants::header_etag);

        auto& headers = http->get_headers();
        for (auto iter = headers.begin(); iter != headers.end(); ++iter)
        {
            if (iter->first.find("x-ms-metadata-") == 0)
            {
                containerProperty.metadata.push_back(std::make_pair(iter->first, iter->second));
            }
        }
    }
    else
    {
        containerProperty.set_valid(false);
    }
    return storage_outcome<container_property>(containerProperty);
}

std::future<storage_outcome<list_constainers_segmented_response>> blob_client::list_containers_segmented(const std::string &prefix, const std::string& continuation_token, const int max_result, bool include_metadata)
{
    auto http = m_client->get_handle();

    auto request = std::make_shared<list_containers_request>(prefix, include_metadata);
    request->set_maxresults(max_result);
    request->set_marker(continuation_token);

    return async_executor<list_constainers_segmented_response>::submit(m_account, request, http, m_context);
}

std::future<storage_outcome<list_blobs_segmented_response>> blob_client::list_blobs_segmented(const std::string &container, const std::string &delimiter, const std::string &continuation_token, const std::string &prefix, int max_results)
{
    auto http = m_client->get_handle();

    auto request = std::make_shared<list_blobs_segmented_request>(container, delimiter, continuation_token, prefix);
    request->set_maxresults(max_results);
    request->set_includes(list_blobs_request_base::include::metadata);

    return async_executor<list_blobs_segmented_response>::submit(m_account, request, http, m_context);
}

std::future<storage_outcome<get_block_list_response>> blob_client::get_block_list(const std::string &container, const std::string &blob)
{
    auto http = m_client->get_handle();

    auto request = std::make_shared<get_block_list_request>(container, blob);

    return async_executor<get_block_list_response>::submit(m_account, request, http, m_context);
}

storage_outcome<blob_property> blob_client::get_blob_property(const std::string &container, const std::string &blob)
{
    auto http = m_client->get_handle();

    auto request = std::make_shared<get_blob_property_request>(container, blob);

    auto response = async_executor<void>::submit(m_account, request, http, m_context).get();
    blob_property blobProperty(true);
    if (response.success())
    {
        blobProperty.cache_control = http->get_header(constants::header_cache_control);
        blobProperty.content_disposition = http->get_header(constants::header_content_disposition);
        blobProperty.content_encoding = http->get_header(constants::header_content_encoding);
        blobProperty.content_language = http->get_header(constants::header_content_language);
        blobProperty.content_md5 = http->get_header(constants::header_content_md5);
        blobProperty.content_type = http->get_header(constants::header_content_type);
        blobProperty.etag = http->get_header(constants::header_etag);
        blobProperty.copy_status = http->get_header(constants::header_ms_copy_status);
        blobProperty.last_modified = curl_getdate(http->get_header(constants::header_last_modified).c_str(), NULL);
        std::string::size_type sz = 0;
        std::string contentLength = http->get_header(constants::header_content_length);
        if(contentLength.length() > 0)
        {
            blobProperty.size = std::stoull(contentLength, &sz, 0);
        }

        auto& headers = http->get_headers();
        for (auto iter = headers.begin(); iter != headers.end(); ++iter)
        {
            if (iter->first.find("x-ms-meta-") == 0)
            {
                // We need to strip ten characters from the front of the key to account for "x-ms-meta-", and two characters from the end of the value, to account for the "\r\n".
                blobProperty.metadata.push_back(std::make_pair(iter->first.substr(10), iter->second.substr(0, iter->second.size() - 2)));
            }
        }
    }
    else
    {
        blobProperty.set_valid(false);
    }
    return storage_outcome<blob_property>(blobProperty);
}

storage_outcome<blob_property> blob_client::get_blob_property_ctoken(const std::string& container, const std::string& blob, const std::string& sas_token, const std::atomic<bool>* token, void* cancelFunc)
{
    auto http = m_client->get_handle();
    http->set_progress_function_and_arg(cancelFunc, (void*)token);
    http->set_sas_token(sas_token);

    auto request = std::make_shared<get_blob_property_request>(container, blob);

    auto response = async_executor<void>::submit(m_account, request, http, m_context).get();
    blob_property blobProperty(true);
    if (response.success())
    {
        blobProperty.cache_control = http->get_header(constants::header_cache_control);
        blobProperty.content_disposition = http->get_header(constants::header_content_disposition);
        blobProperty.content_encoding = http->get_header(constants::header_content_encoding);
        blobProperty.content_language = http->get_header(constants::header_content_language);
        blobProperty.content_md5 = http->get_header(constants::header_content_md5);
        blobProperty.content_type = http->get_header(constants::header_content_type);
        blobProperty.etag = http->get_header(constants::header_etag);
        blobProperty.copy_status = http->get_header(constants::header_ms_copy_status);
        blobProperty.last_modified = curl_getdate(http->get_header(constants::header_last_modified).c_str(), NULL);
        std::string::size_type sz = 0;
        std::string contentLength = http->get_header(constants::header_content_length);
        if(contentLength.length() > 0)
        {
            blobProperty.size = std::stoull(contentLength, &sz, 0);
        }

        auto& headers = http->get_headers();
        for (auto iter = headers.begin(); iter != headers.end(); ++iter)
        {
            if (iter->first.find("x-ms-meta-") == 0)
            {
                // We need to strip ten characters from the front of the key to account for "x-ms-meta-", and two characters from the end of the value, to account for the "\r\n".
                blobProperty.metadata.push_back(std::make_pair(iter->first.substr(10), iter->second.substr(0, iter->second.size() - 2)));
            }
        }
    }
    else
    {
        blobProperty.set_valid(false);
    }
    return storage_outcome<blob_property>(blobProperty);
}

std::future<storage_outcome<void>> blob_client::upload_block_from_stream(const std::string &container, const std::string &blob, const std::string &blockid, std::istream &is)
{
    auto http = m_client->get_handle();

    auto request = std::make_shared<put_block_request>(container, blob, blockid);

    auto cur = is.tellg();
    is.seekg(0, std::ios_base::end);
    auto end = is.tellg();
    is.seekg(cur);
    request->set_content_length(static_cast<unsigned int>(end - cur));

    http->set_input_stream(storage_istream(is));

    return async_executor<void>::submit(m_account, request, http, m_context);
}

std::future<storage_outcome<void>> blob_client::put_block_list(const std::string &container, const std::string &blob, const std::vector<put_block_list_request_base::block_item> &block_list, const std::vector<std::pair<std::string, std::string>> &metadata)
{
    auto http = m_client->get_handle();

    auto request = std::make_shared<put_block_list_request>(container, blob);
    request->set_block_list(block_list);
    if (metadata.size() > 0)
    {
        request->set_metadata(metadata);
    }

    return async_executor<void>::submit(m_account, request, http, m_context);
}

std::future<storage_outcome<void>> blob_client::create_append_blob(const std::string &container, const std::string &blob)
{
    auto http = m_client->get_handle();

    auto request = std::make_shared<create_append_blob_request>(container, blob);

    return async_executor<void>::submit(m_account, request, http, m_context);
}

std::future<storage_outcome<void>> blob_client::append_block_from_stream(const std::string &container, const std::string &blob, std::istream &is)
{
    auto http = m_client->get_handle();

    auto request = std::make_shared<append_block_request>(container, blob);

    auto cur = is.tellg();
    is.seekg(0, std::ios_base::end);
    auto end = is.tellg();
    is.seekg(cur);
    request->set_content_length(static_cast<unsigned int>(end - cur));

    http->set_input_stream(storage_istream(is));

    return async_executor<void>::submit(m_account, request, http, m_context);
}

std::future<storage_outcome<void>> blob_client::create_page_blob(const std::string &container, const std::string &blob, unsigned long long size)
{
    auto http = m_client->get_handle();

    auto request = std::make_shared<create_page_blob_request>(container, blob, size);

    return async_executor<void>::submit(m_account, request, http, m_context);
}

std::future<storage_outcome<void>> blob_client::put_page_from_stream(const std::string &container, const std::string &blob, unsigned long long offset, unsigned long long size, std::istream &is)
{
    auto http = m_client->get_handle();

    auto request = std::make_shared<put_page_request>(container, blob);
    if (size > 0)
    {
        request->set_start_byte(offset);
        request->set_end_byte(offset + size - 1);
    }
    else
    {
        request->set_start_byte(offset);
    }

    auto cur = is.tellg();
    is.seekg(0, std::ios_base::end);
    auto end = is.tellg();
    is.seekg(cur);
    auto stream_size = static_cast<unsigned int>(end - cur);
    request->set_content_length(stream_size);

    http->set_input_stream(storage_istream(is));

    return async_executor<void>::submit(m_account, request, http, m_context);
}

std::future<storage_outcome<void>> blob_client::clear_page(const std::string &container, const std::string &blob, unsigned long long offset, unsigned long long size)
{
    auto http = m_client->get_handle();

    auto request = std::make_shared<put_page_request>(container, blob, true);
    if (size > 0)
    {
        request->set_start_byte(offset);
        request->set_end_byte(offset + size - 1);
    }
    else
    {
        request->set_start_byte(offset);
    }

    return async_executor<void>::submit(m_account, request, http, m_context);
}

std::future<storage_outcome<get_page_ranges_response>> blob_client::get_page_ranges(const std::string &container, const std::string &blob, unsigned long long offset, unsigned long long size)
{
    auto http = m_client->get_handle();

    auto request = std::make_shared<get_page_ranges_request>(container, blob);
    if (size > 0)
    {
        request->set_start_byte(offset);
        request->set_end_byte(offset + size - 1);
    }
    else
    {
        request->set_start_byte(offset);
    }

    return async_executor<get_page_ranges_response>::submit(m_account, request, http, m_context);
}

std::future<storage_outcome<void>> blob_client::start_copy(const std::string &sourceContainer, const std::string &sourceBlob, const std::string &destContainer, const std::string &destBlob)
{
    auto http = m_client->get_handle();

    auto request = std::make_shared<copy_blob_request>(sourceContainer, sourceBlob, destContainer, destBlob);

    return async_executor<void>::submit(m_account, request, http, m_context);
}

}}
