from graphviz import Source
filename = "test"
path = "/Users/andrejpistek/Developer/UT/sylvan/cmake-build-debug/test/"
s = Source.from_file(path + filename + ".gv")
s.view(filename, "/Users/andrejpistek/Developer/UT/sylvan/test/")