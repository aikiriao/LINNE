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

CODEC_LABEL_PREFIXES = ['FLAC', 'WavPack', 'TTA', 'Monkey\'s Audio', 'MPEG4-ALS', 'NARU', 'LINNE']
COLORLIST = ['r', 'g', 'b', 'c', 'm', 'y', 'k', 'w']
CATEGORIES = ['classic', 'genre', 'jazz', 'popular', 'right', 'total']

if __name__ == "__main__":
    df = pd.read_csv('codec_comparison_summery.csv', index_col=0)

    # デコード速度 v.s. 圧縮率グラフ
    for category in CATEGORIES:
        texts = []
        plt.cla()
        for inx, cprefix in enumerate(CODEC_LABEL_PREFIXES):
            line = [[], []]
            for label in df.keys():
                if label.startswith(cprefix):
                    texts.append(plt.text(df.at[f'{category} mean decode time', label],
                        df.at[f'{category} mean compression rate', label], label[len(cprefix):], size=10))
                    line[0].append(df.at[f'{category} mean decode time', label])
                    line[1].append(df.at[f'{category} mean compression rate', label])
            plt.plot(line[0], line[1], color=COLORLIST[inx], label=cprefix, marker='o')
        adjust_text(texts)
        plt.title(f'Decoding speed v.s. compression rate comparison for {category}')
        plt.xlabel('Average decoding speed (\%)')
        plt.ylabel('Average compression rate (\%)')
        plt.legend()
        plt.grid()
        plt.savefig(f'decodespeed_vs_compressionrate_{category}.pdf')

    # エンコード速度 v.s. 圧縮率グラフ
    for category in CATEGORIES:
        texts = []
        plt.cla()
        for inx, cprefix in enumerate(CODEC_LABEL_PREFIXES):
            line = [[], []]
            for label in df.keys():
                if label.startswith(cprefix):
                    texts.append(plt.text(df.at[f'{category} mean encode time', label],
                        df.at[f'{category} mean compression rate', label], label[len(cprefix):], size=10))
                    line[0].append(df.at[f'{category} mean encode time', label])
                    line[1].append(df.at[f'{category} mean compression rate', label])
            plt.plot(line[0], line[1], color=COLORLIST[inx], label=cprefix, marker='o')
        adjust_text(texts)
        plt.title(f'Encoding speed v.s. compression rate comparison for {category}')
        plt.xlabel('Average encoding speed (\%)')
        plt.ylabel('Average compression rate (\%)')
        plt.legend()
        plt.grid()
        plt.savefig(f'encodespeed_vs_compressionrate_{category}.pdf')
