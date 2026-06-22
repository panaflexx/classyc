/* classy-curl.cy — CURL-like command line HTTP client
 *
 * Supports curl-like options:
 *   -X/--request METHOD   Specify request method (GET, POST, PUT, DELETE, etc.)
 *   -d/--data DATA        HTTP POST data
 *   -H/--header HEADER    Extra header to include in the request
 *   -o/--output FILE      Write output to FILE instead of stdout
 *   -O/--remote-name      Write output to a file named as the remote file
 *   -i/--include          Include the HTTP response headers in the output
 *   -I/--head             Fetch headers only (HEAD request)
 *   -v/--verbose          Make the operation more talkative
 *   URL                   The URL to fetch
 *
 * Usage:  classyc examples/classy-curl.cy -eg [options] URL
 * Examples:
 *   classyc examples/classy-curl.cy -eg https://httpbin.org/get
 *   classyc examples/classy-curl.cy -eg -X POST -d '{"key":"value"}' https://httpbin.org/post
 *   classyc examples/classy-curl.cy -eg -o response.json https://httpbin.org/json
 *   classyc examples/classy-curl.cy -eg -O https://httpbin.org/uuid
 *   classyc examples/classy-curl.cy -eg -i https://httpbin.org/headers
 *   classyc examples/classy-curl.cy -eg -I https://httpbin.org/get
 *   classyc examples/classy-curl.cy -eg -v https://httpbin.org/get
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "include/httpclient.h"
#include "include/list.h"

/* Simple config structure for our curl-like options */
class CurlConfig {
    String method;
    String data;
    List<String>* headers;
    String output_file;
    int remote_name;
    int include_headers;
    int head_request;
    int verbose;

    CurlConfig() {
        this->method = "GET";
        this->data = "";
        this->headers = new List<String>();
        this->output_file = "";
        this->remote_name = 0;
        this->include_headers = 0;
        this->head_request = 0;
        this->verbose = 0;
    }

    ~CurlConfig() {
        delete this->headers;
    }
};

void print_usage() {
    printf("Usage: classyc examples/classy-curl.cy -eg [options] URL\n");
    printf("Options:\n");
    printf("  -X, --request <method>   Specify request method to use\n");
    printf("  -d, --data <data>        HTTP POST data\n");
    printf("  -H, --header <header>    Extra header to include in the request\n");
    printf("  -o, --output <file>      Write output to <file> instead of stdout\n");
    printf("  -O, --remote-name        Write output to a file named as the remote file\n");
    printf("  -i, --include            Include the HTTP response headers in the output\n");
    printf("  -I, --head               Fetch headers only (HEAD request)\n");
    printf("  -v, --verbose            Make the operation more talkative\n");
    printf("  URL                      The URL to fetch\n");
    printf("\nExamples:\n");
    printf("  classy-curl.cy -eg https://httpbin.org/get\n");
    printf("  classy-curl.cy -eg -X POST -d '{\"key\":\"value\"}' https://httpbin.org/post\n");
    printf("  classy-curl.cy -eg -o response.json https://httpbin.org/json\n");
    printf("  classy-curl.cy -eg -O https://httpbin.org/uuid\n");
    printf("  classy-curl.cy -eg -i https://httpbin.org/headers\n");
    printf("  classy-curl.cy -eg -I https://httpbin.org/get\n");
    printf("  classy-curl.cy -eg -v https://httpbin.org/get\n");
}

/* Extract filename from URL by finding last '/' */
String extract_filename_from_url(String url) {
    int len = (int)url.length();
    int i;
    for (i = len - 1; i >= 0; i--) {
        if (((char *)url)[i] == '/') {
            break;
        }
    }
    if (i >= 0 && i < len - 1) {
        return url.substr(i + 1, len - i - 1);
    }
    return "index.html";
}

int main(int argc, char *argv[]) {

    if (argc < 2) {
        print_usage();
        return 1;
    }

    CurlConfig* config = new CurlConfig();
    defer delete config;

    String url = "";
    int i = 1;

    while (i < argc) {
        String arg = argv[i];

        if (arg.equals("--help") || arg.equals("-h")) {
            print_usage();
            return 0;
        }
        else if (arg.equals("-X") || arg.equals("--request")) {
            i = i + 1;
            if (i >= argc) {
                printf("error: %s requires an argument\n", (char *)arg);
                return 1;
            }
            config->method = argv[i];
            i = i + 1;
        }
        else if (arg.equals("-d") || arg.equals("--data")) {
            i = i + 1;
            if (i >= argc) {
                printf("error: %s requires an argument\n", (char *)arg);
                return 1;
            }
            config->data = argv[i];
            i = i + 1;
        }
        else if (arg.equals("-H") || arg.equals("--header")) {
            i = i + 1;
            if (i >= argc) {
                printf("error: %s requires an argument\n", (char *)arg);
                return 1;
            }
            config->headers->Add(argv[i]);
            i = i + 1;
        }
        else if (arg.equals("-o") || arg.equals("--output")) {
            i = i + 1;
            if (i >= argc) {
                printf("error: %s requires an argument\n", (char *)arg);
                return 1;
            }
            config->output_file = argv[i];
            i = i + 1;
        }
        else if (arg.equals("-O") || arg.equals("--remote-name")) {
            config->remote_name = 1;
            i = i + 1;
        }
        else if (arg.equals("-i") || arg.equals("--include")) {
            config->include_headers = 1;
            i = i + 1;
        }
        else if (arg.equals("-I") || arg.equals("--head")) {
            config->head_request = 1;
            i = i + 1;
        }
        else if (arg.equals("-v") || arg.equals("--verbose")) {
            config->verbose = 1;
            i = i + 1;
        }
        else {
            /* Treat as URL */
            if (url.length() == 0) {
                url = arg;
            } else {
                printf("error: unexpected argument: %s\n", (char *)arg);
                return 1;
            }
            i = i + 1;
        }
    }

    if (url.length() == 0) {
        printf("error: URL is required\n");
        return 1;
    }

    /* Handle remote name option */
    if (config->remote_name && config->output_file.length() == 0) {
        config->output_file = extract_filename_from_url(url);
    }

    /* Adjust method for HEAD request */
    if (config->head_request) {
        config->method = "HEAD";
    }

    if (config->verbose) {
        printf("classyc curl-like client\n");
        printf("Method: %s\n", (char *)config->method);
        printf("URL: %s\n", (char *)url);
        if (config->data.length() > 0) {
            printf("Data: %s\n", (char *)config->data);
        }
        if (config->headers->Count() > 0) {
            printf("Headers:\n");
            for (auto header in config->headers) {
                printf("  %s\n", (char *)header);
            }
        }
        if (config->output_file.length() > 0) {
            printf("Output: %s\n", (char *)config->output_file);
        }
        if (config->include_headers) {
            printf("Include headers: yes\n");
        }
        if (config->head_request) {
            printf("Head request: yes\n");
        }
        printf("\n");
    }

    try {
        HttpResponse* resp = NULL;

        /* Use the general request method to support headers and body for all methods */
        if (strcmp((char *)config->method, "HEAD") == 0) {
            /* For HEAD, we can use GET but then discard the body */
            resp = Http.request("GET", (char *)url, config->headers, NULL);
        } else {
            resp = Http.request((char *)config->method, (char *)url, config->headers,
                               (config->data.length() > 0) ? (char *)config->data : NULL);
        }

        defer delete resp;

        if (config->verbose) {
            printf("Status: %d %s\n", resp->status, (char *)resp->statusText);

            // Print response headers
            auto headers = resp->headerNames();
            defer delete headers;
            for (auto name in headers) {
                String value = resp->header((char *)name);
                printf("%s: %s\n", (char *)name, (char *)value);
            }
            printf("\n");
        }

        // Handle output
        FILE* out = stdout;
        int should_close = 0;

        if (config->output_file.length() > 0) {
            out = fopen((char *)config->output_file, "w");
            if (out == NULL) {
                printf("Error: Cannot open output file '%s'\n", (char *)config->output_file);
                return 1;
            }
            should_close = 1;
        }

        // Include headers in output if requested
        if (config->include_headers) {
            fprintf(out, "HTTP/%d %d %s\r\n",
                    1, resp->status, (char *)resp->statusText);

            auto headers = resp->headerNames();
            defer delete headers;
            for (auto name in headers) {
                String value = resp->header((char *)name);
                fprintf(out, "%s: %s\r\n", (char *)name, (char *)value);
            }
            fprintf(out, "\r\n");
        }

        // Output body (unless it's a HEAD request and we're not including headers)
        if (!(config->head_request && !config->include_headers)) {
            if (resp->length() > 0) {
                fwrite((char *)resp->body, 1, resp->length(), out);
                if (config->include_headers || config->output_file.length() == 0) {
                    fprintf(out, "\n");
                }
            } else if (!config->include_headers && config->output_file.length() == 0) {
                fprintf(out, "(No response body)\n");
            }
        }

        if (should_close) {
            fclose(out);
            out = NULL;
        }

        if (!resp->ok()) {
            printf("Request failed with status %d\n", resp->status);
            return resp->status;
        }
    } catch (Exception e) {
        printf("Exception caught: id=%u msg=\"%s\"\n", e.id, e.msg);
        return 1;
    }

    if (config->verbose) {
        printf("\nDone.\n");
    }
    return 0;
}
