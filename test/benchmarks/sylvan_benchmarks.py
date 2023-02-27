import subprocess
from enum import Enum
from dataclasses import dataclass
import os
import matplotlib.pyplot as plt
import pandas as pd
import seaborn as sns
from cffi.backend_ctypes import xrange
from matplotlib import pyplot
from pandas import DataFrame

sns.set_theme(style="darkgrid")

MODELS_PATH = "models/"
LINEAR_PROBING_EXECUTABLES_PATH = "executables/linear_probing"
CHAINING_EXECUTABLES_PATH = "executables/linear_probing"
USE_FILE_CSV = False


class HashTableType(Enum):
    UNKNOWN = "UNKNOWN"
    CHAINING = "chaining"
    LINEAR_PROBING = "linear probing"


class ExecutableTypes(Enum):
    UNKNOWN = "UNKNOWN"
    LDD = "lddmc"
    BDD = "bddmc"


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
    hash_table_type: HashTableType = HashTableType.UNKNOWN


def get_model_list(dir_path: str, type: ModelType) -> [str]:
    res = []
    for path in os.listdir(dir_path):
        # check if current path is a file
        if os.path.isfile(os.path.join(dir_path, path)):
            if path.endswith(type.value):
                res.append(dir_path + path)
    print(f"using the following models: {res}")
    return res


def run_example(executable_path: str, model_path: str, arguments: str, type: ModelType,
                hash_table_type: HashTableType) -> Model:
    executable_name = ExecutableTypes.LDD.value if type == ModelType.LDD else ExecutableTypes.BDD.value
    cmd = f"{executable_path}/{executable_name} {model_path} {arguments}"
    print(f"running: {cmd}")
    result = subprocess.check_output(f"{cmd}", shell=True, text=True)
    print(result)
    sat_time = [x.split(":")[1] for x in filter(lambda p: "SAT" in p, result.split("\n"))][0]
    return Model(float(sat_time), model_path, executable_path, type, hash_table_type)


def models_to_dataframe(models: [Model]) -> DataFrame:
    d = {
        'model name': [model.name.split('/')[-1] for model in models],
        'execution time': [x.time for x in models],
        'hash_table_type': [model.hash_table_type.value for model in models]
    }
    return DataFrame(data=d)


def run_models(executable_path: str,
               models_path: str,
               model_type: ModelType,
               hash_table_type: HashTableType) -> DataFrame:
    models: [Model] = []
    for file in get_model_list(models_path, model_type):
        model = run_example(executable_path, file, "", model_type, hash_table_type)
        models.append(model)
    return models_to_dataframe(models)


def y_step(low: float, up: float, leng: int):
    step = ((up - low) * 1.0 / leng)
    return [low + i * step for i in range(leng)]


if __name__ == "__main__":
    if USE_FILE_CSV:
        data = pd.read_csv("data.csv")
    else:
        dataset = []
        for _ in range(10):
            dataset.append(run_models(LINEAR_PROBING_EXECUTABLES_PATH,
                                      MODELS_PATH,
                                      ModelType.LDD,
                                      HashTableType.LINEAR_PROBING))
            dataset.append(run_models(LINEAR_PROBING_EXECUTABLES_PATH,
                                      MODELS_PATH,
                                      ModelType.BDD,
                                      HashTableType.LINEAR_PROBING))
            dataset.append(run_models(CHAINING_EXECUTABLES_PATH,
                                      MODELS_PATH,
                                      ModelType.LDD,
                                      HashTableType.CHAINING))
            dataset.append(run_models(CHAINING_EXECUTABLES_PATH,
                                      MODELS_PATH,
                                      ModelType.BDD,
                                      HashTableType.CHAINING))
        data = pd.concat(dataset)
        data.to_csv("data.csv")

    pyplot.figure(figsize=(35, 25))
    g = sns.lineplot(x="model name", y="execution time", hue="hash_table_type", data=data)
    g.set_yticks(y_step(0.1, 11.5, 100))
    plt.show()
