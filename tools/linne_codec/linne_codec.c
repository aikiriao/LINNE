#include <linne_encoder.h>
#include <linne_decoder.h>
#include "wav.h"
#include "command_line_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* a, bのうち小さい方を選択 */
#define LINNECODEC_MIN(a, b) (((a) < (b)) ? (a) : (b))

/* コマンドライン仕様 */
static struct CommandLineParserSpecification command_line_spec[] = {
    { 'e', "encode", "Encode mode",
        COMMAND_LINE_PARSER_FALSE, NULL, COMMAND_LINE_PARSER_FALSE },
    { 'd', "decode", "Decode mode",
        COMMAND_LINE_PARSER_FALSE, NULL, COMMAND_LINE_PARSER_FALSE },
    { 'm', "mode", "Specify compress mode: 0(fast), ..., 3(high compression) default:0",
        COMMAND_LINE_PARSER_TRUE, NULL, COMMAND_LINE_PARSER_FALSE },
    { 'l', "enable-learning", "Whether to learning at encoding (default:no)",
        COMMAND_LINE_PARSER_FALSE, NULL, COMMAND_LINE_PARSER_FALSE },
    { 'a', "auxiliary-function-iteration", "Specify auxiliary function method iteration count (default:0)",
        COMMAND_LINE_PARSER_TRUE, "0", COMMAND_LINE_PARSER_FALSE },
    { 'c', "no-crc-check", "Whether to NOT check CRC16 at decoding (default:no)",
        COMMAND_LINE_PARSER_FALSE, NULL, COMMAND_LINE_PARSER_FALSE },
    { 'h', "help", "Show command help message",
        COMMAND_LINE_PARSER_FALSE, NULL, COMMAND_LINE_PARSER_FALSE },
    { 'v', "version", "Show version information",
        COMMAND_LINE_PARSER_FALSE, NULL, COMMAND_LINE_PARSER_FALSE },
    { 0, NULL,  }
};

/* エンコード 成功時は0、失敗時は0以外を返す */
static int do_encode(
    const char* in_filename, const char* out_filename,
    uint32_t encode_preset_no, uint8_t enable_learning, uint8_t num_afmethod_iterations)
{
    FILE *out_fp;
    struct WAVFile *in_wav;
    struct LINNEEncoder *encoder;
    struct LINNEEncoderConfig config;
    struct LINNEEncodeParameter parameter;
    struct stat fstat;
    int32_t *input[LINNE_MAX_NUM_CHANNELS];
    uint8_t *buffer;
    uint32_t buffer_size, encoded_data_size;
    LINNEApiResult ret;
    uint32_t ch, smpl, num_channels, num_samples;

    /* エンコーダ作成 */
    config.max_num_channels = LINNE_MAX_NUM_CHANNELS;
    config.max_num_samples_per_block = 16 * 1024;
    config.max_num_layers = 5;
    config.max_num_parameters_per_layer = 128;
    if ((encoder = LINNEEncoder_Create(&config, NULL, 0)) == NULL) {
        fprintf(stderr, "Failed to create encoder handle. \n");
        return 1;
    }

    /* WAVファイルオープン */
    if ((in_wav = WAV_CreateFromFile(in_filename)) == NULL) {
        fprintf(stderr, "Failed to open %s. \n", in_filename);
        return 1;
    }
    num_channels = in_wav->format.num_channels;
    num_samples = in_wav->format.num_samples;

    /* エンコードパラメータセット */
    parameter.num_channels = (uint16_t)num_channels;
    parameter.bits_per_sample = (uint16_t)in_wav->format.bits_per_sample;
    parameter.sampling_rate = in_wav->format.sampling_rate;
    /* プリセットの反映 */
    parameter.num_samples_per_block = 5 * 2048;
    parameter.ch_process_method = LINNE_CH_PROCESS_METHOD_MS;
    parameter.preset = (uint8_t)encode_preset_no;
    parameter.enable_learning = enable_learning;
    parameter.num_afmethod_iterations = num_afmethod_iterations;
    /* 2ch未満の信号にはMS処理できないので無効に */
    if (num_channels < 2) {
        parameter.ch_process_method = LINNE_CH_PROCESS_METHOD_NONE;
    }
    if ((ret = LINNEEncoder_SetEncodeParameter(encoder, &parameter)) != LINNE_APIRESULT_OK) {
        fprintf(stderr, "Failed to set encode parameter: %d \n", ret);
        return 1;
    }

    /* 入力ファイルのサイズを拾っておく */
    stat(in_filename, &fstat);
    /* 入力wavの2倍よりは大きくならないだろうという想定 */
    buffer_size = (uint32_t)(2 * fstat.st_size);

    /* エンコードデータ/入力データ領域を作成 */
    buffer = (uint8_t *)malloc(buffer_size);
    for (ch = 0; ch < num_channels; ch++) {
        input[ch] = (int32_t *)malloc(sizeof(int32_t) * num_samples);
    }

    /* 情報が失われない程度に右シフト */
    for (ch = 0; ch < num_channels; ch++) {
        for (smpl = 0; smpl < num_samples; smpl++) {
            input[ch][smpl] = (int32_t)(WAVFile_PCM(in_wav, smpl, ch) >> (32 - in_wav->format.bits_per_sample));
        }
    }

    /* エンコード実行 */
    {
        uint8_t *data_pos = buffer;
        uint32_t write_offset, progress;
        struct LINNEHeader header;

        write_offset = 0;

        /* ヘッダエンコード */
        header.num_channels = (uint16_t)num_channels;
        header.num_samples = num_samples;
        header.sampling_rate = parameter.sampling_rate;
        header.bits_per_sample = parameter.bits_per_sample;
        header.num_samples_per_block = parameter.num_samples_per_block;
        header.preset = parameter.preset;
        header.ch_process_method = parameter.ch_process_method;
        if ((ret = LINNEEncoder_EncodeHeader(&header, data_pos, buffer_size))
                != LINNE_APIRESULT_OK) {
            fprintf(stderr, "Failed to encode header! ret:%d \n", ret);
            return 1;
        }
        data_pos += LINNE_HEADER_SIZE;
        write_offset += LINNE_HEADER_SIZE;

        /* ブロックを時系列順にエンコード */
        progress = 0;
        while (progress < num_samples) {
            uint32_t ch, write_size;
            const int32_t *input_ptr[LINNE_MAX_NUM_CHANNELS];
            /* エンコードサンプル数の確定 */
            const uint32_t num_encode_samples
                = LINNECODEC_MIN(parameter.num_samples_per_block, num_samples - progress);

            /* サンプル参照位置のセット */
            for (ch = 0; ch < (uint32_t)num_channels; ch++) {
                input_ptr[ch] = &input[ch][progress];
            }

            /* ブロックエンコード */
            if ((ret = LINNEEncoder_EncodeBlock(encoder,
                            input_ptr, num_encode_samples,
                            data_pos, buffer_size - write_offset, &write_size)) != LINNE_APIRESULT_OK) {
                fprintf(stderr, "Failed to encode! ret:%d \n", ret);
                return 1;
            }

            /* 進捗更新 */
            data_pos += write_size;
            write_offset += write_size;
            progress += num_encode_samples;

            /* 進捗表示 */
            printf("progress... %5.2f%% \r", (progress * 100.0f) / num_samples);
            fflush(stdout);
        }

        /* 書き出しサイズ取得 */
        encoded_data_size = write_offset;
    }

    /* ファイル書き出し */
    out_fp = fopen(out_filename, "wb");
    if (fwrite(buffer, sizeof(uint8_t), encoded_data_size, out_fp) < encoded_data_size) {
        fprintf(stderr, "File output error! %d \n", ret);
        return 1;
    }

    /* 圧縮結果サマリの表示 */
    printf("finished: %d -> %d (%6.2f %%) \n",
            (uint32_t)fstat.st_size, encoded_data_size, 100.f * (double)encoded_data_size / fstat.st_size);

    /* リソース破棄 */
    fclose(out_fp);
    free(buffer);
    for (ch = 0; ch < num_channels; ch++) {
        free(input[ch]);
    }
    WAV_Destroy(in_wav);
    LINNEEncoder_Destroy(encoder);

    return 0;
}

/* デコード 成功時は0、失敗時は0以外を返す */
static int do_decode(const char* in_filename, const char* out_filename, uint8_t check_crc)
{
    FILE* in_fp;
    struct WAVFile* out_wav;
    struct WAVFileFormat wav_format;
    struct stat fstat;
    struct LINNEDecoder* decoder;
    struct LINNEDecoderConfig config;
    struct LINNEHeader header;
    uint8_t* buffer;
    uint32_t ch, smpl, buffer_size;
    LINNEApiResult ret;

    /* デコーダハンドルの作成 */
    config.max_num_channels = LINNE_MAX_NUM_CHANNELS;
    config.max_num_layers = 5;
    config.max_num_parameters_per_layer = 128;
    config.check_crc = check_crc;
    if ((decoder = LINNEDecoder_Create(&config, NULL, 0)) == NULL) {
        fprintf(stderr, "Failed to create decoder handle. \n");
        return 1;
    }

    /* 入力ファイルオープン */
    in_fp = fopen(in_filename, "rb");
    /* 入力ファイルのサイズ取得 / バッファ領域割り当て */
    stat(in_filename, &fstat);
    buffer_size = (uint32_t)fstat.st_size;
    buffer = (uint8_t *)malloc(buffer_size);
    /* バッファ領域にデータをロード */
    fread(buffer, sizeof(uint8_t), buffer_size, in_fp);
    fclose(in_fp);

    /* ヘッダデコード */
    if ((ret = LINNEDecoder_DecodeHeader(buffer, buffer_size, &header))
            != LINNE_APIRESULT_OK) {
        fprintf(stderr, "Failed to get header information: %d \n", ret);
        return 1;
    }

    /* 出力wavハンドルの生成 */
    wav_format.data_format     = WAV_DATA_FORMAT_PCM;
    wav_format.num_channels    = header.num_channels;
    wav_format.sampling_rate   = header.sampling_rate;
    wav_format.bits_per_sample = header.bits_per_sample;
    wav_format.num_samples     = header.num_samples;
    if ((out_wav = WAV_Create(&wav_format)) == NULL) {
        fprintf(stderr, "Failed to create wav handle. \n");
        return 1;
    }

    /* 一括デコード */
    if ((ret = LINNEDecoder_DecodeWhole(decoder,
                    buffer, buffer_size,
                    (int32_t **)out_wav->data, out_wav->format.num_channels, out_wav->format.num_samples))
                != LINNE_APIRESULT_OK) {
        fprintf(stderr, "Decoding error! %d \n", ret);
        return 1;
    }

    /* エンコード時に右シフトした分を戻し、32bit化 */
    for (ch = 0; ch < out_wav->format.num_channels; ch++) {
        for (smpl = 0; smpl < out_wav->format.num_samples; smpl++) {
            WAVFile_PCM(out_wav, smpl, ch) <<= (32 - out_wav->format.bits_per_sample);
        }
    }

    /* WAVファイル書き出し */
    if (WAV_WriteToFile(out_filename, out_wav) != WAV_APIRESULT_OK) {
        fprintf(stderr, "Failed to write wav file. \n");
        return 1;
    }

    free(buffer);
    WAV_Destroy(out_wav);
    LINNEDecoder_Destroy(decoder);

    return 0;
}

/* 使用法の表示 */
static void print_usage(char** argv)
{
    printf("Usage: %s [options] INPUT_FILE_NAME OUTPUT_FILE_NAME \n", argv[0]);
}

/* バージョン情報の表示 */
static void print_version_info(void)
{
    printf("LINNE -- LInear-predictive Neural Net Encoder Version.%d \n", LINNE_CODEC_VERSION);
}

/* メインエントリ */
int main(int argc, char** argv)
{
    const char* filename_ptr[2] = { NULL, NULL };
    const char* input_file;
    const char* output_file;

    /* 引数が足らない */
    if (argc == 1) {
        print_usage(argv);
        /* 初めて使った人が詰まらないようにヘルプの表示を促す */
        printf("Type `%s -h` to display command helps. \n", argv[0]);
        return 1;
    }

    /* コマンドライン解析 */
    if (CommandLineParser_ParseArguments(command_line_spec,
                argc, (const char* const*)argv, filename_ptr, sizeof(filename_ptr) / sizeof(filename_ptr[0]))
            != COMMAND_LINE_PARSER_RESULT_OK) {
        return 1;
    }

    /* ヘルプやバージョン情報の表示判定 */
    if (CommandLineParser_GetOptionAcquired(command_line_spec, "help") == COMMAND_LINE_PARSER_TRUE) {
        print_usage(argv);
        printf("options: \n");
        CommandLineParser_PrintDescription(command_line_spec);
        return 0;
    } else if (CommandLineParser_GetOptionAcquired(command_line_spec, "version") == COMMAND_LINE_PARSER_TRUE) {
        print_version_info();
        return 0;
    }

    /* 入力ファイル名の取得 */
    if ((input_file = filename_ptr[0]) == NULL) {
        fprintf(stderr, "%s: input file must be specified. \n", argv[0]);
        return 1;
    }

    /* 出力ファイル名の取得 */
    if ((output_file = filename_ptr[1]) == NULL) {
        fprintf(stderr, "%s: output file must be specified. \n", argv[0]);
        return 1;
    }

    /* エンコードとデコードは同時に指定できない */
    if ((CommandLineParser_GetOptionAcquired(command_line_spec, "decode") == COMMAND_LINE_PARSER_TRUE)
            && (CommandLineParser_GetOptionAcquired(command_line_spec, "encode") == COMMAND_LINE_PARSER_TRUE)) {
        fprintf(stderr, "%s: encode and decode mode cannot specify simultaneously. \n", argv[0]);
        return 1;
    }

    if (CommandLineParser_GetOptionAcquired(command_line_spec, "decode") == COMMAND_LINE_PARSER_TRUE) {
        /* デコード */
        uint8_t crc_check = 1;
        /* CRC無効フラグを取得 */
        if (CommandLineParser_GetOptionAcquired(command_line_spec, "no-crc-check") == COMMAND_LINE_PARSER_TRUE) {
            crc_check = 0;
        }
        /* 一括デコード実行 */
        if (do_decode(input_file, output_file, crc_check) != 0) {
            fprintf(stderr, "%s: failed to decode %s. \n", argv[0], input_file);
            return 1;
        }
    } else if (CommandLineParser_GetOptionAcquired(command_line_spec, "encode") == COMMAND_LINE_PARSER_TRUE) {
        /* エンコード */
        uint32_t encode_preset_no = 0;
        uint8_t enable_learning = 0;
        uint8_t num_afmethod_iterations = 0;
        /* エンコードプリセット番号取得 */
        if (CommandLineParser_GetOptionAcquired(command_line_spec, "mode") == COMMAND_LINE_PARSER_TRUE) {
            encode_preset_no = (uint32_t)strtol(CommandLineParser_GetArgumentString(command_line_spec, "mode"), NULL, 10);
            if (encode_preset_no >= LINNE_NUM_PARAMETER_PRESETS) {
                fprintf(stderr, "%s: encode preset number is out of range. \n", argv[0]);
                return 1;
            }
        }
        /* 学習フラグを取得 */
        if (CommandLineParser_GetOptionAcquired(command_line_spec, "enable-learning") == COMMAND_LINE_PARSER_TRUE) {
            enable_learning = 1;
        }
        /* 補助関数法の繰り返し回数を取得 */
        if (CommandLineParser_GetOptionAcquired(command_line_spec, "auxiliary-function-iteration") == COMMAND_LINE_PARSER_TRUE) {
            num_afmethod_iterations = (uint8_t)strtol(CommandLineParser_GetArgumentString(command_line_spec, "auxiliary-function-iteration"), NULL, 10);
            if (num_afmethod_iterations >= UINT8_MAX) {
                fprintf(stderr, "%s: auxiliary function iteration count is out of range. \n", argv[0]);
                return 1;
            }
        }
        /* 一括エンコード実行 */
        if (do_encode(input_file, output_file, encode_preset_no, enable_learning, num_afmethod_iterations) != 0) {
            fprintf(stderr, "%s: failed to encode %s. \n", argv[0], input_file);
            return 1;
        }
    } else {
        fprintf(stderr, "%s: decode(-d) or encode(-e) option must be specified. \n", argv[0]);
        return 1;
    }

    return 0;
}
