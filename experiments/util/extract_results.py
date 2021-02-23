


def extract_result(result_path):

    result_file = open(result_path, 'r')

    keywords = {
        "Pass 1 time:" : "pass_1",
        "Pass 1e time:" : "pass_1e",
        "Pass 2 time:" : "pass_2",
        "Pass 3 time:" : "pass_3",
        "Pass 4 time:" : "pass_4",
        "Pass 5 time:" : "pass_5",
        "Total Time:" : "total"
    }

    result = {}

    for line in result_file:
        for key, val in keywords.items():
            if line.startswith(key):
                seconds = line.replace(key, "")
                seconds = seconds.replace("seconds","")
                seconds = seconds.replace(" ","")
                seconds = seconds.replace("\n","")
                result[val] = seconds
                break

    #print result

    return result
