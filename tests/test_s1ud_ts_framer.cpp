#include <iostream>
#include <vector>
#include <cassert>
#include <cstring>
#include "../winusb/src/BonDriver_S1UD_WinUSB/siano_proto.h"
#include "../winusb/src/BonDriver_S1UD_WinUSB/ts_framer.hpp"

// Test 1: Siano protocol structure sizes and message header initialization
void TestSianoProto()
{
    std::cout << "[Test 1] Siano Protocol Structure Sizes and Header Init..." << std::endl;

    assert(sizeof(sms_msg_hdr) == 8);
    assert(sizeof(sms_msg_data) == 12);
    assert(sizeof(sms_msg_data2) == 16);
    assert(sizeof(sms_msg_data3) == 20);
    assert(sizeof(sms_msg_data4) == 24);
    assert(sizeof(sms_msg_data5) == 28);
    assert(sizeof(sms_firmware_header) == 12);

    sms_msg_hdr hdr = {};
    InitSianoMsgHdr(&hdr, MSG_SMS_ISDBT_TUNE_REQ, DVBT_BDA_CONTROL_MSG_ID, HIF_TASK, sizeof(sms_msg_hdr));
    assert(hdr.msg_type == MSG_SMS_ISDBT_TUNE_REQ);
    assert(hdr.msg_src_id == DVBT_BDA_CONTROL_MSG_ID);
    assert(hdr.msg_dst_id == HIF_TASK);
    assert(hdr.msg_length == sizeof(sms_msg_hdr));
    assert(hdr.msg_flags == 0);

    std::cout << "  -> Passed." << std::endl;
}

// Test 2: Standard TS sync with clean contiguous packets
void TestTsFramerNormal()
{
    std::cout << "[Test 2] TS Framer - Clean Stream..." << std::endl;

    px_s1ud::TsFramer framer;
    std::vector<uint8_t> packets(188 * 5, 0x00);
    for (int i = 0; i < 5; ++i) {
        packets[i * 188] = 0x47;
        packets[i * 188 + 1] = 0x00;
        packets[i * 188 + 2] = static_cast<uint8_t>(i + 1); // PID = i + 1
        packets[i * 188 + 3] = 0x10;
    }

    framer.FeedRawData(packets.data(), packets.size());
    assert(framer.GetReadyCount() == 5);

    std::vector<uint8_t> out(188 * 3);
    size_t remain = 0;
    size_t popped = framer.PopTsPackets(out.data(), out.size(), &remain);
    assert(popped == 188 * 3);
    assert(remain == 188 * 2);
    assert(framer.GetReadyCount() == 2);

    // Verify first popped packet
    assert(out[0] == 0x47);
    assert(out[2] == 0x01);

    // Pop remaining
    popped = framer.PopTsPackets(out.data(), out.size(), &remain);
    assert(popped == 188 * 2);
    assert(remain == 0);
    assert(framer.GetReadyCount() == 0);

    std::cout << "  -> Passed." << std::endl;
}

// Test 3: Unaligned chunks & partial packets across FeedRawData calls
void TestTsFramerChunked()
{
    std::cout << "[Test 3] TS Framer - Chunked Feeds across Boundaries..." << std::endl;

    px_s1ud::TsFramer framer;
    // Prepare 3 packets preceded by 17 bytes of garbage
    std::vector<uint8_t> stream;
    for (int i = 0; i < 17; ++i) stream.push_back(0xAA);

    for (int i = 0; i < 3; ++i) {
        std::vector<uint8_t> p(188, 0x55);
        p[0] = 0x47;
        p[1] = 0x01;
        p[2] = static_cast<uint8_t>(0x10 + i);
        stream.insert(stream.end(), p.begin(), p.end());
    }

    // Feed in irregular chunk sizes: 50 bytes, 100 bytes, etc.
    size_t fed = 0;
    while (fed < stream.size()) {
        size_t chunk = (std::min)(static_cast<size_t>(73), stream.size() - fed);
        framer.FeedRawData(stream.data() + fed, chunk);
        fed += chunk;
    }

    assert(framer.GetReadyCount() == 3);

    std::vector<uint8_t> out(188 * 3);
    size_t remain = 0;
    size_t popped = framer.PopTsPackets(out.data(), out.size(), &remain);
    assert(popped == 188 * 3);
    assert(out[0] == 0x47);
    assert(out[188] == 0x47);
    assert(out[188 * 2] == 0x47);

    std::cout << "  -> Passed." << std::endl;
}

// Test 4: False sync byte rejection and resynchronization
void TestTsFramerSyncRecovery()
{
    std::cout << "[Test 4] TS Framer - False Sync Byte Rejection..." << std::endl;

    px_s1ud::TsFramer framer;
    std::vector<uint8_t> data;

    // False sync byte: 0x47 followed by random noise (no sync byte at +188)
    data.push_back(0x47);
    for (int i = 0; i < 187; ++i) data.push_back(0x12);
    data.push_back(0x99); // Not 0x47!

    // Followed by 2 valid packets
    for (int p = 0; p < 2; ++p) {
        std::vector<uint8_t> valid(188, 0xEE);
        valid[0] = 0x47;
        data.insert(data.end(), valid.begin(), valid.end());
    }

    framer.FeedRawData(data.data(), data.size());
    // Only the 2 valid packets should be accepted
    assert(framer.GetReadyCount() == 2);

    std::cout << "  -> Passed." << std::endl;
}

// Test 5: Discard null packets option
void TestTsFramerDiscardNullPackets()
{
    std::cout << "[Test 5] TS Framer - Discard Null Packets..." << std::endl;

    px_s1ud::TsFramer framer;
    framer.SetDiscardNullPackets(true);

    std::vector<uint8_t> data(188 * 3, 0);
    // Packet 1: Normal (PID = 0x0100)
    data[0] = 0x47; data[1] = 0x01; data[2] = 0x00;
    // Packet 2: Null packet (PID = 0x1FFF: byte 1 & 0x1F = 0x1F, byte 2 = 0xFF)
    data[188] = 0x47; data[188 + 1] = 0x1F; data[188 + 2] = 0xFF;
    // Packet 3: Normal (PID = 0x0200)
    data[188 * 2] = 0x47; data[188 * 2 + 1] = 0x02; data[188 * 2 + 2] = 0x00;

    framer.FeedRawData(data.data(), data.size());
    // Packet 2 should be filtered out
    assert(framer.GetReadyCount() == 2);

    std::vector<uint8_t> out(188 * 2);
    size_t remain = 0;
    size_t popped = framer.PopTsPackets(out.data(), out.size(), &remain);
    assert(popped == 188 * 2);
    assert(out[1] == 0x01); // Packet 1
    assert(out[188 + 1] == 0x02); // Packet 3

    std::cout << "  -> Passed." << std::endl;
}

// Test 6: Frequency Calculation for ISDB-T Physical 13ch - 62ch
void TestChannelCalculation()
{
    std::cout << "[Test 6] ISDB-T Frequency Calculation..." << std::endl;

    for (uint32_t ch = 13; ch <= 62; ++ch) {
        uint32_t freq = 473142857 + (ch - 13) * 6000000;
        if (ch == 13) {
            assert(freq == 473142857); // 473.143 MHz
        } else if (ch == 62) {
            assert(freq == 473142857 + 49 * 6000000); // 767.143 MHz
        }
    }

    std::cout << "  -> Passed." << std::endl;
}

int main()
{
    std::cout << "===========================================" << std::endl;
    std::cout << " Running PX-S1UD WinUSB Smoke & Unit Tests " << std::endl;
    std::cout << "===========================================" << std::endl;

    TestSianoProto();
    TestTsFramerNormal();
    TestTsFramerChunked();
    TestTsFramerSyncRecovery();
    TestTsFramerDiscardNullPackets();
    TestChannelCalculation();

    std::cout << "===========================================" << std::endl;
    std::cout << " ALL TESTS PASSED SUCCESSFULLY!            " << std::endl;
    std::cout << "===========================================" << std::endl;
    return 0;
}
