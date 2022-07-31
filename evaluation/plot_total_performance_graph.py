" コーデック評価グラフの作成 "
import pandas as pd
import matplotlib
import matplotlib.pyplot as plt
from adjustText import adjust_text

# type3 font 回避(tex使用)
matplotlib.rcParams['text.usetex'] = True
matplotlib.rcParams['text.latex.preamble'] = '\\usepackage{sfmath}'
# フォントサイズ一括設定
matplotlib.rcParams["font.size"] = 12

CODEC_LABEL_PREFIXES = ['Flac', 'WavPack', 'TTA', 'Monkey\'s Audio', 'MPEG4-ALS', 'LINNE']
COLORLIST = ['r', 'g', 'b', 'c', 'm', 'y', 'k', 'w']

if __name__ == "__main__":
    df = pd.read_csv('codec_comparison_summery.csv', index_col=0)
    # デコード速度 v.s. 圧縮率グラフ
    texts = []
    for inx, cprefix in enumerate(CODEC_LABEL_PREFIXES):
        line = [[], []]
        for label in df.keys():
            if label.startswith(cprefix):
                texts.append(plt.text(df.at['Total mean decode time', label],
                    df.at['Total mean compression rate', label], label[len(cprefix):], size=10))
                line[0].append(df.at['Total mean decode time', label])
                line[1].append(df.at['Total mean compression rate', label])
        plt.plot(line[0], line[1], color=COLORLIST[inx], label=cprefix, marker='o')
    adjust_text(texts)
    plt.title('Decoding speed v.s. compression rate comparison')
    plt.xlabel('Total average decoding speed (\%)')
    plt.ylabel('Total average compression rate (\%)')
    plt.legend()
    plt.grid()
    plt.savefig('codec_comparison_decodespeed_vs_compressionrate.pdf')
