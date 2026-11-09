#ifndef SIANO_PROTO_H_
#define SIANO_PROTO_H_

#include <cstdint>

#pragma pack(push, 1)

// Siano communication definitions
constexpr uint8_t  HIF_TASK                = 11;
constexpr uint8_t  DVBT_BDA_CONTROL_MSG_ID = 201;

constexpr uint16_t SMS_MAX_PAYLOAD_SIZE    = 240;

// Device modes
enum SianoDeviceMode : int32_t {
    DEVICE_MODE_NONE      = -1,
    DEVICE_MODE_DVBT      = 0,
    DEVICE_MODE_DVBH      = 1,
    DEVICE_MODE_DAB_TDMB  = 2,
    DEVICE_MODE_DAB_TDMB_DABIP = 3,
    DEVICE_MODE_DVBT_BDA  = 4,
    DEVICE_MODE_ISDBT     = 5,
    DEVICE_MODE_ISDBT_BDA = 6,
    DEVICE_MODE_CMMB      = 7,
    DEVICE_MODE_RAW_TUNER = 8,
    DEVICE_MODE_FM_RADIO  = 9,
    DEVICE_MODE_FM_RADIO_BDA = 10,
    DEVICE_MODE_ATSC      = 11,
    DEVICE_MODE_MAX       = 12
};

// Bandwidth modes for tuning
enum SianoBandwidthMode : uint32_t {
    BW_8_MHZ       = 0,
    BW_7_MHZ       = 1,
    BW_6_MHZ       = 2,
    BW_5_MHZ       = 3,
    BW_ISDBT_1SEG  = 4,
    BW_ISDBT_3SEG  = 5,
    BW_2_MHZ       = 6,
    BW_FM_RADIO    = 7,
    BW_ISDBT_13SEG = 8,
    BW_1_5_MHZ     = 15,
    BW_UNKNOWN     = 0xffff
};

// Message Types
enum SianoMsgType : uint16_t {
    MSG_TYPE_BASE_VAL                           = 500,
    MSG_SMS_GET_VERSION_REQ                     = 503,
    MSG_SMS_GET_VERSION_RES                     = 504,
    MSG_SMS_SET_MAX_TX_MSG_LEN_REQ              = 516,
    MSG_SMS_SET_MAX_TX_MSG_LEN_RES              = 517,
    MSG_SMS_INIT_DEVICE_REQ                     = 578,
    MSG_SMS_INIT_DEVICE_RES                     = 579,
    MSG_SMS_ADD_PID_FILTER_REQ                  = 601,
    MSG_SMS_ADD_PID_FILTER_RES                  = 602,
    MSG_SMS_REMOVE_PID_FILTER_REQ               = 603,
    MSG_SMS_REMOVE_PID_FILTER_RES               = 604,
    MSG_SMS_GET_PID_FILTER_LIST_REQ             = 608,
    MSG_SMS_GET_PID_FILTER_LIST_RES             = 609,
    MSG_SMS_POWER_DOWN_REQ                      = 610,
    MSG_SMS_POWER_DOWN_RES                      = 611,
    MSG_SMS_GET_STATISTICS_REQ                  = 615,
    MSG_SMS_GET_STATISTICS_RES                  = 616,
    MSG_SMS_GET_STATISTICS_EX_REQ               = 653,
    MSG_SMS_GET_STATISTICS_EX_RES               = 654,
    MSG_SMS_DATA_DOWNLOAD_REQ                   = 660,
    MSG_SMS_DATA_DOWNLOAD_RES                   = 661,
    MSG_SMS_DATA_VALIDITY_REQ                   = 662,
    MSG_SMS_DATA_VALIDITY_RES                   = 663,
    MSG_SMS_SWDOWNLOAD_TRIGGER_REQ              = 664,
    MSG_SMS_SWDOWNLOAD_TRIGGER_RES              = 665,
    MSG_SMS_GET_VERSION_EX_REQ                  = 668,
    MSG_SMS_GET_VERSION_EX_RES                  = 669,
    MSG_SMS_DVBT_BDA_DATA                       = 693,
    MSG_SW_RELOAD_REQ                           = 697,
    MSG_SMS_DATA_MSG                            = 699,
    MSG_SW_RELOAD_START_REQ                     = 702,
    MSG_SW_RELOAD_START_RES                     = 703,
    MSG_SW_RELOAD_EXEC_REQ                      = 704,
    MSG_SW_RELOAD_EXEC_RES                      = 705,
    MSG_SMS_ISDBT_TUNE_REQ                      = 776,
    MSG_SMS_ISDBT_TUNE_RES                      = 777,
    MSG_SMS_SIGNAL_DETECTED_IND                 = 827,
    MSG_SMS_NO_SIGNAL_IND                       = 828
};

struct sms_msg_hdr {
    uint16_t msg_type;
    uint8_t  msg_src_id;
    uint8_t  msg_dst_id;
    uint16_t msg_length; // Total length including header
    uint16_t msg_flags;
};

struct sms_msg_data {
    sms_msg_hdr x_msg_header;
    uint32_t msg_data;
};

struct sms_msg_data2 {
    sms_msg_hdr x_msg_header;
    uint32_t msg_data[2];
};

struct sms_msg_data3 {
    sms_msg_hdr x_msg_header;
    uint32_t msg_data[3];
};

struct sms_msg_data4 {
    sms_msg_hdr x_msg_header;
    uint32_t msg_data[4];
};

struct sms_msg_data5 {
    sms_msg_hdr x_msg_header;
    uint32_t msg_data[5];
};

struct sms_data_download {
    sms_msg_hdr x_msg_header;
    uint32_t    mem_addr;
    uint8_t     payload[SMS_MAX_PAYLOAD_SIZE];
};

struct sms_version_res {
    sms_msg_hdr x_msg_header;
    uint16_t    chip_model;
    uint8_t     step;
    uint8_t     metal_fix;
    uint8_t     firmware_id;
    uint8_t     supported_protocols;
    uint8_t     version_major;
    uint8_t     version_minor;
    uint8_t     version_patch;
    uint8_t     version_field_patch;
    uint8_t     rom_ver_major;
    uint8_t     rom_ver_minor;
    uint8_t     rom_ver_patch;
    uint8_t     rom_ver_field_patch;
    uint8_t     text_label[34];
};

struct sms_firmware_header {
    uint32_t check_sum;
    uint32_t length;
    uint32_t start_address;
    // followed by payload bytes
};

struct sms_isdbt_layer_stats {
    uint32_t code_rate;
    uint32_t constellation;
    uint32_t ber;
    uint32_t ber_error_count;
    uint32_t ber_bit_count;
    uint32_t pre_ber;
    uint32_t ts_per;
    uint32_t error_ts_packets;
    uint32_t total_ts_packets;
    uint32_t ti_ldepth_i;
    uint32_t number_of_segments;
    uint32_t tmcc_errors;
};

struct sms_isdbt_stats {
    uint32_t statistics_type;
    uint32_t full_size;
    uint32_t is_rf_locked;
    uint32_t is_demod_locked;
    uint32_t is_external_lna_on;
    int32_t  snr;
    int32_t  rssi;
    int32_t  in_band_pwr;
    int32_t  carrier_offset;
    uint32_t frequency;
    uint32_t bandwidth;
    uint32_t transmission_mode;
    uint32_t modem_state;
    uint32_t guard_interval;
    uint32_t system_type;
    uint32_t partial_reception;
    uint32_t num_of_layers;
    sms_isdbt_layer_stats layer_info[3];
    uint32_t sms_to_host_tx_errors;
};

struct sms_isdbt_stats_ex {
    uint32_t statistics_type;
    uint32_t full_size;
    uint32_t is_rf_locked;
    uint32_t is_demod_locked;
    uint32_t is_external_lna_on;
    int32_t  snr;
    int32_t  rssi;
    int32_t  in_band_pwr;
    int32_t  carrier_offset;
    uint32_t frequency;
    uint32_t bandwidth;
    uint32_t transmission_mode;
    uint32_t modem_state;
    uint32_t guard_interval;
    uint32_t system_type;
    uint32_t partial_reception;
    uint32_t num_of_layers;
    uint32_t segment_number;
    uint32_t tune_bw;
    sms_isdbt_layer_stats layer_info[3];
    uint32_t reserved1;
    uint32_t ext_antenna;
    uint32_t reception_quality;
    uint32_t ews_alert_active;
    uint32_t lna_on_off;
    uint32_t rf_agc_level;
    uint32_t bb_agc_level;
    uint32_t fw_errors_counter;
    uint8_t  fw_errors_history_arr[8];
    int32_t  mrc_snr;
    uint32_t snr_full_res;
    uint32_t reserved4[4];
};

#pragma pack(pop)

inline void InitSianoMsgHdr(sms_msg_hdr *hdr, uint16_t type, uint8_t src, uint8_t dst, uint16_t len)
{
    hdr->msg_type = type;
    hdr->msg_src_id = src;
    hdr->msg_dst_id = dst;
    hdr->msg_length = len;
    hdr->msg_flags = 0;
}

#endif // SIANO_PROTO_H_
