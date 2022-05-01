import sys
import argparse
import json
import matplotlib.pyplot as plt

# FIXME: untested

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
    data = results['data']

    cp_x = data['cp']['buffer_size']
    cp_y = data['cp']['time']

    fcp_x = data['fcp']['buffer_size']
    fcp_y = data['fcp']['time']

    plt.plot(cp_x, cp_y, fcp_x, fcp_y)
    plt.show()

def main():
    parsed = get_parsed()
    filename = parsed.filename
    plot(filename)

if __name__ == '__main__':
    main()
