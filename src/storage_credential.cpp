#include "storage_credential.h"

#include "base64.h"
#include "constants.h"
#include "hash.h"
#include "utility.h"

namespace azure {  namespace storage_lite {

    shared_key_credential::shared_key_credential(const std::string &account_name, const std::string &account_key)
        : m_account_name(account_name),
        m_account_key(from_base64(account_key)) {}

    shared_key_credential::shared_key_credential(const std::string &account_name, const std::vector<unsigned char> &account_key)
        : m_account_name(account_name),
        m_account_key(account_key) {}

    void shared_key_credential::sign_request(const storage_request_base &, http_base &h, const storage_url &url, const storage_headers &headers) const
    {
        std::string string_to_sign(get_http_verb(h.get_method()));
        string_to_sign.append("\n");

        string_to_sign.append(headers.content_encoding).append("\n");
        string_to_sign.append(headers.content_language).append("\n");
        string_to_sign.append(headers.content_length).append("\n");
        string_to_sign.append(headers.content_md5).append("\n");
        string_to_sign.append(headers.content_type).append("\n");
        // TODO: add date to string_to_sign when date is supported.
        string_to_sign.append("\n"); // Date
        string_to_sign.append(headers.if_modified_since).append("\n");
        string_to_sign.append(headers.if_match).append("\n");
        string_to_sign.append(headers.if_none_match).append("\n");
        string_to_sign.append(headers.if_unmodified_since).append("\n");
        string_to_sign.append("\n"); // Range

        // Canonicalized headers
        for (const auto &header : headers.ms_headers)
        {
            string_to_sign.append(header.first).append(":").append(header.second).append("\n");
        }

        // Canonicalized resource
        string_to_sign.append("/").append(m_account_name).append(url.get_encoded_path());
        for (const auto &name : url.get_query()) {
            string_to_sign.append("\n").append(name.first);
            bool first_value = true;
            for (const auto &value : name.second) {
                if (first_value) {
                    string_to_sign.append(":");
                    first_value = false;
                }
                else {
                    string_to_sign.append(",");
                }
                string_to_sign.append(value);
            }
        }

        std::string authorization("SharedKey ");
        authorization.append(m_account_name).append(":").append(hash(string_to_sign, m_account_key));
        h.add_header(constants::header_authorization, authorization);
    }

    void shared_key_credential::sign_request(const table_request_base &, http_base &, const storage_url &, const storage_headers &) const {}

    std::string shared_access_signature_credential::transform_url(std::string url) const
    {
        if (url.find('?') != std::string::npos) {
            url.append("&");
        }
        else {
            url.append("?");
        }
        url.append(m_sas_token);
        return url;
    }

    // RESCALE HACKED FUNCTION
    // Does not sign a request if it already has been signed, used so that SAS credentials can be
    // changed after creating a request without affecting request retries.
    void shared_access_signature_credential::sign_request(const storage_request_base &, http_base &h, const storage_url &, const storage_headers &) const
    {
        std::string token = h.get_sas_token();
        std::string url = h.get_url();
        if (token.empty()) {
            std::string transformed_url = transform_url(url);
            h.set_sas_token(m_sas_token);
            h.set_url(transformed_url);
        } else {
            if (url.find('?') != std::string::npos) {
                url.append("&");
            }
            else {
                url.append("?");
            }
            url.append(token);
            h.set_url(url);
        }
    }
}}   // azure::storage_lite
