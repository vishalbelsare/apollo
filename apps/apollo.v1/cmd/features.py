import argparse
import warnings

import numpy as np
import pandas as pd

from sklearn.ensemble import RandomForestClassifier
from sklearn.pipeline import Pipeline

import apollo
from apollo.pipeline import get_pipeline_steps
from apollo.transformers import DataframePipeline, AutoDataFrameMapper

from apollo.codegen import CodeGenerator
from apollo.util.timer import Timer


def setup_parser(subparser):
    subparser.add_argument(
        '-p', '--predict', action='store', dest='predict',
        help="Select which label to predict for: policy or thread")
    subparser.add_argument(
        'files', nargs=argparse.REMAINDER, help="files containing application samples and instruction data")


def get_features(app_data, instruction_data, kind, features):
    steps = get_pipeline_steps(
        kind=kind, data=instruction_data,
        dropped_features=getattr(apollo, features))

    steps = [x for x in steps if x[0] != 'threads']

    pipeline = DataframePipeline(steps)
    X, y = pipeline.fit_transform(app_data)

    pipeline = Pipeline([
        ('mapper', AutoDataFrameMapper()),
        ('clf', RandomForestClassifier())])

    pipeline.fit(X,y)

    features = AutoDataFrameMapper().get_feature_list(X)
    forest = pipeline.steps[-1][1]
    importances = forest.feature_importances_
    std = np.std([tree.feature_importances_ for tree in forest.estimators_],
                axis=0)
    indices = np.argsort(importances)[::-1]

    features = [features[indices[f]] for f in range(min(15, len(features)))]
    importances = [importances[indices[f]] for f in range(min(15, len(features)))]

    return features, importances


def features(parser, args):
    warnings.simplefilter("ignore")

    if not args.files:
        sys.stderr.write("install requires two files of application samples\n")
        sys.exit(-1)

    app_data_name, instruction_data_name = (args.files[0], args.files[1])
    app_data, instruction_data = apollo.util.loader.load(app_data_name, instruction_data_name)

    features, importances = get_features(app_data, instruction_data, args.predict, 'dropped_features')

    print("Feature ranking:")
    for count,(f,i) in enumerate(zip(features, importances)):
        print("%d. feature %s (%f)" % (count, f, i))
