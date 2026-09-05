#!/bin/bash

git push origin master && sleep 5 &&  ./scripts/fetch-artifact.sh --wait && ./scripts/deploy-artifact.sh
