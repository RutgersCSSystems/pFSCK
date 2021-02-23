import os, sys
import xml.etree.ElementTree as ET
from math import log

# Test Attributes
test_name = "name"

# Run Attributes
run_name = "name"
run_key = "key"
run_image = "image"
run_flags = "flags"
run_build_flags = "buildflags"
# Graph Attributes
graph_title = "title"
graph_xlabel = "xlabel"
graph_ylabel = "ylabel"


class Build(object):
    def __init__(self):
        pass

class Run(object):
    def __init__(self):
        pass
    def set_flags(self):
        pass
    def set_build(self):
        pass

class Graph(object):
    def __init__(self):
        pass

def expand_dymanic_elements(xml_path, elements):

    for element in elements:
        if len(element) > 0:
            expand_dymanic_elements(xml_path, element)
        elif element.text != None:
            changed = False
            if "$" in element.text:
                #print("Expanding vars for " + element.text + " in " + xml_path \
                #      + " to " + os.path.expandvars(element.text))
                element.text = os.path.expandvars(element.text)
                changed = True
            while "[" in element.text and "]" in element.text:
                start = element.text.index("[")
                end = element.text.index("]")
                expression = element.text[start + 1:end]
                #print("Evaluating expression " + expression + " in " + \
                #      element.text + " to " + str(eval(expression)))

                #expanded = str(os.path.expandvars(expression))
                #print("expanded " + expanded)
                evaluated = eval(expression)
                #print("evaluated " + str(evaluated))
                #print("current tag is " + element.tag)
                #new_tag = element.tag.replace("dynamic_", "")
                new_text = str(evaluated).join(element.text.split(element.text[start:end+1]))

                #print("Dynamic XML Element found: replacing <" + element.tag + ", " + \
                #      element.text + "> with <" + new_tag + ", " + new_text + ">" + \
                #      " in " + xml_path)
                #element.tag = new_tag
                element.text = new_text
                changed = True
            #if changed:
             #   print("Final <element, text> pair:  <" + element.tag + ", " + \
              #         element.text + ">")




def parse_XML(test_path):

    # Get root of XML
    tree = ET.parse(test_path)
    root = tree.getroot()

    runs   = []
    graphs = []

    name = root.find("name").text

    expand_dymanic_elements(test_path, root)

    for run in root.findall("run"):
        run_info = {}

        for element in run:
            if element.text != None:
                run_info[element.tag] = element.text
            else:
                run_info[element.tag] = ""

        runs.append(run_info)

    # Parse all graphs
    for graph in root.findall("graph"):
        graph_info = {
            graph_title : graph.find(graph_title).text,
            graph_xlabel : graph.find(graph_xlabel).text,
            graph_ylabel : graph.find(graph_ylabel).text
        }
        fields = []
        fields_element = graph.find("fields")
        for field_element in fields_element:
            fields.append(field_element.text)

        graph_info["fields"] = fields

        graphs.append(graph_info)

    #print(runs)
    #print(graphs)

    #exit()

    return {"name": name, "runs" : runs, "graphs" : graphs}
