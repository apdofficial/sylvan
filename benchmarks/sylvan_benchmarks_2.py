import matplotlib.pyplot as plt
import pandas as pd
import seaborn as sns
from matplotlib import pyplot

sns.set_theme(style="darkgrid")


def step(low: float, up: float, leng: int):
    step = ((up - low) * 1.0 / leng)
    return [low + i * step for i in range(leng)]


if __name__ == "__main__":
    dataset_chaining = pd.read_csv("./hashmap_cmp/chaining/hashmap_chaining_medians.csv")
    dataset_probing = pd.read_csv("./hashmap_cmp/probing/limited/hashmap_probing_medians.csv")
    dataset_probing_unlimited = pd.read_csv("./hashmap_cmp/probing/unlimited/hashmap_probing_medians.csv")

    pyplot.figure(figsize=(10, 10))

    g = sns.lineplot(x="usages", y="runtimes", data=dataset_chaining,  legend='brief', label="Chaining")
    sns.lineplot(x="usages", y="runtimes", data=dataset_probing, legend='brief', label="Probing")
    sns.lineplot(x="usages", y="runtimes", data=dataset_probing_unlimited, legend='brief', label="Probing unlimited")

    g.set_yticks(step(0.1, 230, 25))
    g.set_xticks(step(0.1, 100, 15))

    plt.show()
