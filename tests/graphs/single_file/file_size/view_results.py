import sys
import argparse
import json
import matplotlib.pyplot as plt

def get_parsed():
    parser = argparse.ArgumentParser()
    parser.add_argument('-f', dest='filename', required=True, help="File path of the results file", type=str)
    parsed = parser.parse_args(sys.argv[1:])
    return parsed

def get_results(path):
    with open(path) as fh:
        return json.loads(fh.read())

def plot(path):
    results = get_results(path)
    cp_x = [x/1024 for x in results['cp']['size']]
    cp_y = [x/1024 for x in results['cp']['time']]

    fcp_x = [x/1024 for x in results['fcp']['size']]
    fcp_y = [x/1024 for x in results['fcp']['time']]

    #! TODO: Add x, y axes labels
    plt.plot(cp_x, cp_y, fcp_x, fcp_y)
    plt.show()

def main():
    parsed = get_parsed()
    filename = parsed.filename
    plot(filename)

if __name__ == '__main__':
    main()
