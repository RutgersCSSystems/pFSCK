import numpy as np
import matplotlib
matplotlib.use('Agg') # needed to allow for used from ssh shell
import matplotlib.pyplot as plt
import csv
import sys




# Creates a Horizontal Bar Chart
# given fields
def HorizontalStackedBarChart(csv_path, title, x_label, y_label, fields, output):


    # Open CSV file
    csv_file = open(csv_path, 'r')

    # Read CSV into a dictionary
    rows = csv.DictReader(csv_file, delimiter=',')

    #dictionary for lists
    fields_dict = {field : [] for field in fields}

    # Get x
    runs = []
    total_runs = 0

    # Get fields we want
    for row in rows:

        runs.append(row["Key"])
        for field in fields:
            fields_dict[field].append(float(row[field]))
        total_runs += 1



    # Calculate properties for bars
    width = 0.35
    ind = np.arange(total_runs)

    # Create bars from fields
    bars = []
    total = [0] * len(runs)
    for field in fields:
        bars.append(plt.barh(ind, fields_dict[field], width, left=total))
        total = [x + y for x,y in zip(fields_dict[field],total)]

    # Set graph stuff
    plt.xlabel(x_label)
    plt.ylabel(y_label)
    plt.title(title)
    plt.gca().invert_yaxis()
    plt.yticks(ind, tuple(runs))
    plt.legend(tuple([x[0] for x in bars]), tuple(fields))

    # Save Graph
    plt.savefig(output, bbox_inches='tight')

    # Show graph
    # plt.show()

# Creates a Horizontal Bar Chart
# given fields
def VerticleStackedBarChart(csv_path, title, x_label, y_label, fields, output):


    # Open CSV file
    csv_file = open(csv_path, 'r')

    # Read CSV into a dictionary
    rows = csv.DictReader(csv_file, delimiter=',')

    #dictionary for lists
    fields_dict = {field : [] for field in fields}

    # Get x
    runs = []
    total_runs = 0

    # Get fields we want
    for row in rows:

        runs.append(row["Key"])
        for field in fields:
            fields_dict[field].append(float(row[field]))
        total_runs += 1



    # Calculate properties for bars
    width = 0.35
    ind = np.arange(total_runs)

    # Create bars from fields
    colors = ["r", "b", "g", "y", "c", "m", "k", "w"]
    bars = []
    total = [0] * len(runs)
    color_index = 0
    for field in fields:
        bars.append(plt.bar(ind, fields_dict[field], width, color=colors[color_index], bottom=total))
        total = [x + y for x,y in zip(fields_dict[field],total)]
        color_index += 1

    # Set graph stuff
    plt.xlabel(x_label)
    plt.ylabel(y_label)
    #plt.title(title)
    plt.xticks(ind, tuple(runs), rotation='vertical')
    plt.legend(tuple([x[0] for x in bars]), tuple(fields))

    # Save Graph
    plt.savefig(output, bbox_inches='tight')

    # Show graph
    # plt.show()


def GroupedBarChart():
    pass
