import sys
import argparse
import json
import matplotlib.pyplot as plt

def get_parsed():
    parser = argparse.ArgumentParser()
    parser.add_argument('-f', dest='filename', required=True, help="File path of the results file")

def get_results(path):
    with open(path) as fh:
        return json.loads(path)

def plot(path):
    results = get_results(path)
    cp_x = results['cp']['size']
    cp_y = results['cp']['time']

    fcp_x = results['fcp']['size']
    fcp_y = results['fcp']['time']

    plt.plot(cp_x, cp_y, fcp_x, fcp_y)
    plt.show()

def main():
    parsed = get_parsed()
    filename = parsed.filename

if __name__ == '__main__':
    main()
