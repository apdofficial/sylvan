import os
import subprocess
from enum import Enum
from dataclasses import dataclass
import matplotlib as mpl
import matplotlib.pyplot as plt
import numpy as np


class ModelType(Enum):
    UNKNOWN = "UNKNOWN"
    LDD = "ldd"
    BDD = "bdd"


@dataclass
class Model:
    time: float = 0.0
    name: str = ""
    executable: str = ""
    type: ModelType = ModelType.UNKNOWN


def get_model_list(dir_path: str, type: ModelType) -> [str]:
    print(type.name)
    res = []
    for path in os.listdir(dir_path):
        # check if current path is a file
        if os.path.isfile(os.path.join(dir_path, path)):
            if path.endswith(type.value):
                res.append(dir_path + path)
    return res


def run_example(executable_name: str, model_path: str, arguments: str, type: ModelType) -> Model:
    result = subprocess.check_output(f"./build/examples/{executable_name} {model_path} {arguments}",
                                     shell=True,
                                     text=True)
    sat_time = [x.split(":")[1] for x in filter(lambda p: "SAT" in p, result.split("\n"))][0]
    return Model(float(sat_time), model_path, executable_name, type)


MODELS_PATH = "./models/"

ldd_models: [Model] = []
for file in get_model_list(MODELS_PATH, ModelType.LDD):
    model = run_example("bddmc", file, "-w 8", ModelType.BDD)
    ldd_models.append(model)

bdd_models: [Model] = []
for file in get_model_list(MODELS_PATH, ModelType.BDD):
    bdd_model = run_example("lddmc", file, "-w 8", ModelType.BDD)
    ldd_models.append(bdd_model)

fig, ax = plt.subplots(figsize=(5, 2.7), layout='constrained')
ax.plot(x, x, label='bddmc linear probing')
ax.plot(x, x ** 2, label='bddmc cahining')
ax.plot(x, x ** 3, label='lddmc linear probing')
ax.plot(x, x ** 3, label='lddmc linear probing')

ax.set_xlabel('x label')  # Add an x-label to the axes.
ax.set_ylabel('y label')  # Add a y-label to the axes.
ax.set_title("Simple Plot")  # Add a title to the axes.
ax.legend()
