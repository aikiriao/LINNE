" コーデック評価グラフの作成 "
import pandas as pd
import matplotlib.pyplot as plt
from adjustText import adjust_text

CODEC_LABEL_PREFIXES = ['Flac', 'WavPack', 'TTA', 'Monkey\'s Audio', 'MPEG4-ALS', 'LINNE']
COLORLIST = ['r', 'g', 'b', 'c', 'm', 'y', 'k', 'w']

if __name__ == "__main__":
    df = pd.read_csv('codec_comaprison_summery.csv', index_col=0)
    # デコード速度 v.s. 圧縮率グラフ
    texts = []
    for inx, cprefix in enumerate(CODEC_LABEL_PREFIXES):
        line = [[], []]
        for label in df.keys():
            if label.startswith(cprefix):
                plt.scatter(df.at['Total mean decode time', label], df.at['Total mean compression rate', label], label=label, color=COLORLIST[inx])
                texts.append(plt.text(df.at['Total mean decode time', label], df.at['Total mean compression rate', label], label))
                line[0].append(df.at['Total mean decode time', label])
                line[1].append(df.at['Total mean compression rate', label])
        if len(line[0]) > 1:
            plt.plot(line[0], line[1], color=COLORLIST[inx])
    adjust_text(texts)
    plt.xlabel('Mean decoding speed (%)')
    plt.ylabel('Mean compression rate (%)')
    plt.grid()
    plt.show()
