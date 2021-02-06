#include <iostream>
#include <sstream>
#include <string>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <algorithm>
#include <sys/mman.h>
#include <syslog.h>
#include "bic_vr.h"
#include <openbmc/kv.h>
#ifdef BIC_SUPPORT
#include <facebook/bic.h>
#include <openbmc/pal.h>
using namespace std;

int VrExtComponent::get_ver_str(string& s, const uint8_t alt_fw_comp) {
  char ver[32] = {0};
  uint8_t rbuf[6] = {0};
  int ret = 0;
  ret = bic_get_fw_ver(slot_id, alt_fw_comp, rbuf);
  if ( alt_fw_comp == FW_2OU_PESW_VR ) {
    snprintf(ver, sizeof(ver), "%02X%02X%02X%02X", rbuf[0], rbuf[1], rbuf[2], rbuf[3]);
    s = string(ver);
    s += ", Remaining Writes: " + to_string(rbuf[4]);
  }
  else {
    snprintf(ver, sizeof(ver), "%02X%02X", rbuf[0], rbuf[1]);
    s = string(ver);
  }
  return ret;
}

void VrExtComponent::get_version(json& j) {
  return;
}

int VrExtComponent::print_version()
{
  map<uint8_t, string> list = {{FW_2OU_3V3_VR1, "VR P3V3_STBY1"},
                               {FW_2OU_3V3_VR2, "VR P3V3_STBY2"},
                               {FW_2OU_3V3_VR3, "VR P3V3_STBY3"},
                               {FW_2OU_1V8_VR, "VR P1V8"},
                               {FW_2OU_PESW_VR, "VR P0V84/P0V9"}};
  string ver("");
  string board_name = name;
  string err_msg("");
 
  transform(board_name.begin(), board_name.end(), board_name.begin(), ::toupper);
  try {
    server.ready();
    expansion.ready();
  } catch(string& err) {
    for ( auto& node:list ) {
      printf("%s %s Version: NA (%s)\n", board_name.c_str(), node.second.c_str(), err.c_str());
    }
    return FW_STATUS_SUCCESS;
  }

  for (auto& node:list ) {
    try {
      //Print VR FWs
      if ( get_ver_str(ver, node.first) < 0 ) {
        throw "Error in getting the version of " + board_name;
      }
      cout << board_name << " " << node.second << " Version: " << ver << endl;
    } catch(string& err) {
      printf("%s %s Version: NA (%s)\n", board_name.c_str(), node.second.c_str(), err.c_str());
    }
  } 

  return FW_STATUS_SUCCESS;
}

int VrExtComponent::update(string image)
{
  int ret = 0;
  try {
    server.ready();
    expansion.ready();
    ret = bic_update_fw(slot_id, fw_comp, (char *)image.c_str(), FORCE_UPDATE_UNSET);
  } catch(string& err) {
    cout << "Failed Reason: " << err << endl;
    return FW_STATUS_NOT_SUPPORTED;
  }
  return ret;
}
#endif
