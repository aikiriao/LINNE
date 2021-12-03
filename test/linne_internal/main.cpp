#include <stdlib.h>
#include <string.h>

#include <gtest/gtest.h>

/* テスト対象のモジュール */
extern "C" {
#include "../../libs/linne_internal/src/linne_utility.c"
}

/* CRC16の計算テスト */
TEST(LINNEUtilityTest, CalculateCRC16Test)
{
    /* リファレンス値と一致するか？ */
    {
        uint32_t i;
        uint16_t ret;

        /* テストケース */
        struct CRC16TestCaseFor32BitData {
            uint8_t data[4];
            uint16_t answer;
        };

        static const struct CRC16TestCaseFor32BitData crc16ibm_test_case[] = {
            { { 0x00, 0x00, 0x00, 0x01 }, 0xC0C1 },
            { { 0x10, 0x00, 0x00, 0x00 }, 0xC004 },
            { { 0x00, 0xFF, 0xFF, 0x00 }, 0xC071 },
            { { 0xDE, 0xAD, 0xBE, 0xAF }, 0x159A },
            { { 0xAB, 0xAD, 0xCA, 0xFE }, 0xE566 },
            { { 0x12, 0x34, 0x56, 0x78 }, 0x347B },
        };
        const uint32_t crc16ibm_num_test_cases = sizeof(crc16ibm_test_case) / sizeof(crc16ibm_test_case[0]);

        for (i = 0; i < crc16ibm_num_test_cases; i++) {
            ret = LINNEUtility_CalculateCRC16(crc16ibm_test_case[i].data, sizeof(crc16ibm_test_case[i].data));
            EXPECT_EQ(ret, crc16ibm_test_case[i].answer);
        }
    }

    /* 実データでテスト */
    {
        struct stat fstat;
        uint32_t i, data_size;
        uint16_t ret;
        uint8_t *data;
        FILE *fp;

        /* テストケース */
        struct CRC16TestCaseForFile {
            const char *filename;
            uint16_t answer;
        };

        static const struct CRC16TestCaseForFile crc16ibm_test_case[] = {
            { "a.wav",            0xA611 },
            { "PriChanIcon.png",  0xEA63 },
        };
        const uint32_t crc16ibm_num_test_cases
            = sizeof(crc16ibm_test_case) / sizeof(crc16ibm_test_case[0]);

        for (i = 0; i < crc16ibm_num_test_cases; i++) {
            stat(crc16ibm_test_case[i].filename, &fstat);
            data_size = fstat.st_size;
            data = (uint8_t *)malloc(fstat.st_size * sizeof(uint8_t));

            fp = fopen(crc16ibm_test_case[i].filename, "rb");
            fread(data, sizeof(uint8_t), data_size, fp);
            ret = LINNEUtility_CalculateCRC16(data, data_size);
            EXPECT_EQ(ret, crc16ibm_test_case[i].answer);

            free(data);
            fclose(fp);
        }

    }
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
