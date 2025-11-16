#!/bin/sh
set -e

HOSTS="redis-node-0:10000 redis-node-1:10001 redis-node-2:10002 redis-node-3:10003 redis-node-4:10004 redis-node-5:10005"

echo "Waiting for Redis nodes to accept connections..."
for host in $HOSTS; do
  until redis-cli -h "$(echo $host | cut -d: -f1)" -p "$(echo $host | cut -d: -f2)" ping >/dev/null 2>&1; do
    sleep 1
  done
done

echo "Creating cluster..."
yes yes | redis-cli --cluster create $HOSTS --cluster-replicas 1

echo "Redis cluster configured."
