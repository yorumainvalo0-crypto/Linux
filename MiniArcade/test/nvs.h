#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <map>
#include <string>
typedef int esp_err_t2;
typedef uint32_t nvs_handle_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define NVS_READONLY 0
#define NVS_READWRITE 1
extern std::map<std::string,uint16_t> simNvsU16;
extern std::map<std::string,std::string> simNvsBlob;
inline int nvs_open(const char*, int, nvs_handle_t* h){ *h=1; return ESP_OK; }
inline void nvs_close(nvs_handle_t){}
inline int nvs_commit(nvs_handle_t){ return ESP_OK; }
inline int nvs_get_u16(nvs_handle_t, const char* k, uint16_t* v){
  auto i=simNvsU16.find(k); if(i==simNvsU16.end()) return ESP_FAIL; *v=i->second; return ESP_OK; }
inline int nvs_set_u16(nvs_handle_t, const char* k, uint16_t v){ simNvsU16[k]=v; return ESP_OK; }
inline int nvs_get_blob(nvs_handle_t, const char* k, void* buf, size_t* n){
  auto i=simNvsBlob.find(k); if(i==simNvsBlob.end()||i->second.size()!=*n) return ESP_FAIL;
  memcpy(buf,i->second.data(),*n); return ESP_OK; }
inline int nvs_set_blob(nvs_handle_t, const char* k, const void* b, size_t n){
  simNvsBlob[k]=std::string((const char*)b,n); return ESP_OK; }
