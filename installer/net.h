#pragma once

// HTTPS GET into memory over WinHTTP (system proxy settings, redirects followed).

#include <atomic>
#include <string>
#include <vector>

// progress (optional) receives bytes done / total (total 0 while unknown). False + error text on failure.
bool HttpGet(const std::wstring& url, std::vector<unsigned char>* out, std::string* error,
             std::atomic<long long>* done = nullptr, std::atomic<long long>* total = nullptr);
