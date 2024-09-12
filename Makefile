# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2023. University of Texas at Austin. All rights reserved.

SHELL := /bin/bash

UNAME ?= $(shell whoami)
UID ?= $(shell id -u)
GID ?= $(shell id -g)

BASE_IMAGE_NAME ?= galois
IMAGE_NAME ?= ${UNAME}-${BASE_IMAGE_NAME}
SRC_DIR ?= $(shell pwd)
VERSION ?= $(shell git log --pretty="%h" -1 Dockerfile.dev)

CONTAINER_SRC_DIR ?= /galois
CONTAINER_BUILD_DIR ?= /galois/build
CONTAINER_WORKDIR ?= ${CONTAINER_SRC_DIR}
CONTAINER_CONTEXT ?= default
CONTAINER_OPTS ?=
CONTAINER_CMD ?= bash -l
INTERACTIVE ?= i

BUILD_TYPE ?= Release

# CMake variables
GALOIS_TEST_DISCOVERY_TIMEOUT ?= 150
GALOIS_EXTRA_CXX_FLAGS ?= ""

# Developer variables that should be set as env vars in startup files like .profile
GALOIS_CONTAINER_MOUNTS ?=
GALOIS_CONTAINER_ENV ?=
GALOIS_CONTAINER_FLAGS ?=
GALOIS_BUILD_TOOL ?= 'Unix Makefiles'
GALOIS_CCACHE_DIR ?= ${SRC_DIR}/.ccache

# Variables for SonarQube
PROFILE ?= observability
CONTAINER_SONAR_DIR ?= /galois/build-sonar
CONTAINER_SONAR_CMD ?= ${CONTAINER_CMD} -c "make sonar-scan"
SONAR_EXE ?= sonar-scanner
SONAR_PROJECT_VERSION ?= -Dsonar.projectVersion=0.1.0
define sonar_ip
$(shell kubectl --context ${PROFILE} get nodes --namespace sonarqube -o jsonpath="{.items[0].status.addresses[0].address}")
endef
define sonar_port
$(shell kubectl --context ${PROFILE} get --namespace sonarqube -o jsonpath="{.spec.ports[0].nodePort}" services sonarqube-sonarqube)
endef

dependencies: dependencies-asdf

dependencies-asdf:
	@echo "Updating asdf plugins..."
	@asdf plugin update --all >/dev/null 2>&1 || true
	@echo "Adding new asdf plugins..."
	@cut -d" " -f1 ./.tool-versions | xargs -I % asdf plugin-add % >/dev/null 2>&1 || true
	@echo "Installing asdf tools..."
	@cat ./.tool-versions | xargs -I{} bash -c 'asdf install {}'
	@echo "Updating local environment to use proper tool versions..."
	@cat ./.tool-versions | xargs -I{} bash -c 'asdf local {}'
	@asdf reshim
	@echo "Done!"

hooks:
	@pre-commit install --hook-type pre-commit
	@pre-commit install-hooks

pre-commit:
	@pre-commit run -a

git-submodules:
	@git submodule update --init --recursive

ci-image:
	@${MAKE} docker-image-dependencies
	@docker image inspect galois:${VERSION} >/dev/null 2>&1 || \
	docker --context ${CONTAINER_CONTEXT} build \
	--build-arg SRC_DIR=${CONTAINER_SRC_DIR} \
	--build-arg BUILD_DIR=${CONTAINER_BUILD_DIR} \
	--build-arg UNAME=runner \
  --build-arg UID=1078 \
  --build-arg GID=504 \
	-t galois:${VERSION} \
	--file Dockerfile.dev \
	--target dev .

docker-image:
	@${MAKE} docker-image-dependencies
	@docker image inspect ${IMAGE_NAME}:${VERSION} >/dev/null 2>&1 || \
	docker --context ${CONTAINER_CONTEXT} build \
	--build-arg SRC_DIR=${CONTAINER_SRC_DIR} \
	--build-arg BUILD_DIR=${CONTAINER_BUILD_DIR} \
	--build-arg UNAME=${UNAME} \
	--build-arg IS_CI=false \
  --build-arg UID=${UID} \
  --build-arg GID=${GID} \
	-t ${IMAGE_NAME}:${VERSION} \
	--file Dockerfile.dev \
	--target dev .

docker-image-dependencies:
	@mkdir -p build
	@mkdir -p data
	@mkdir -p .ccache

.PHONY: docker
docker:
	@docker --context ${CONTAINER_CONTEXT} run --rm \
	-v ${SRC_DIR}/:${CONTAINER_SRC_DIR} \
	-v ${GALOIS_CCACHE_DIR}/:/home/${UNAME}/.ccache \
	${GALOIS_CONTAINER_MOUNTS} \
	${GALOIS_CONTAINER_ENV} \
	${GALOIS_CONTAINER_FLAGS} \
	--privileged \
	--workdir=${CONTAINER_WORKDIR} \
	${CONTAINER_OPTS} \
	-${INTERACTIVE}t \
	${IMAGE_NAME}:${VERSION} \
	${CONTAINER_CMD}

.PHONY: cmake
cmake:
	@echo "Must be run from inside the dev Docker container"
	@cmake \
  -S ${SRC_DIR} \
  -B ${BUILD_DIR} \
	-G ${GALOIS_BUILD_TOOL} \
  -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
	-DCMAKE_CXX_FLAGS=${GALOIS_EXTRA_CXX_FLAGS} \
  -DCMAKE_INSTALL_PREFIX=/opt/galois \
  -DBUILD_TESTING=ON \
	-DGALOIS_TEST_DISCOVERY_TIMEOUT=${GALOIS_TEST_DISCOVERY_TIMEOUT} \
	-DCMAKE_CXX_COMPILER=g++-12 \
  -DCMAKE_C_COMPILER=gcc-12

setup: cmake

setup-ci: cmake

.PHONY: test
test:
	@ctest --test-dir ${BUILD_DIR} --output-on-failure

run-tests: test

smoke-tests:
	@bash scripts/wf4/run.sh

gcovr:
	@echo "Should be run outside a container"
	@echo file://${SRC_DIR}/build-sonar/coverage/html
	@python -mwebbrowser file://${SRC_DIR}/build-sonar/coverage/html

sonar:
	@echo "Should be run outside a container"
	@echo http://$(call sonar_ip):$(call sonar_port)
	@python -mwebbrowser http://$(call sonar_ip):$(call sonar_port)


run-gcovr:
	COVERAGE_CMD='echo done' ${MAKE} run-coverage

run-sonar:
	COVERAGE_CMD="${SONAR_EXE} -Dsonar.host.url=http://$(call sonar_ip):$(call sonar_port) ${SONAR_PROJECT_VERSION}" ${MAKE} run-coverage

run-coverage:
	@docker --context ${CONTAINER_CONTEXT} run --rm \
	-v ${SRC_DIR}/:${CONTAINER_SRC_DIR} \
	${GALOIS_CONTAINER_MOUNTS} \
	${GALOIS_CONTAINER_ENV} \
	-e=COVERAGE_CMD="${COVERAGE_CMD}" \
	--privileged \
	--net=host \
	--workdir=${CONTAINER_WORKDIR} ${CONTAINER_OPTS} -${INTERACTIVE}t \
	${IMAGE_NAME}:${VERSION} \
	${CONTAINER_SONAR_CMD}

sonar-scan:
	@mkdir -p ${CONTAINER_SONAR_DIR}/coverage
	@cmake \
  -S ${SRC_DIR} \
  -B ${CONTAINER_SONAR_DIR} \
	-DCMAKE_C_COMPILER=gcc-12 \
	-DCMAKE_CXX_COMPILER=g++-12 \
  -DCMAKE_BUILD_TYPE=Coverage \
  -DBUILD_TESTING=ON
	@cd ${CONTAINER_SONAR_DIR} && \
	make -j8 && \
	ctest --verbose --output-junit junit.xml
	@cd ${CONTAINER_SONAR_DIR} && \
	gcovr \
	--gcov-executable gcov-12 \
	--root .. \
	--xml-pretty --xml cobertura.xml \
	--sonarqube sonarqube.xml \
	--html-details coverage/html \
	-j8 .
	@${COVERAGE_CMD}

# this command is slow since hooks are not stored in the container image
# this is mostly for CI use
docker-pre-commit:
	@docker --context ${CONTAINER_CONTEXT} run --rm \
	-v ${SRC_DIR}/:${CONTAINER_SRC_DIR} --privileged \
	--workdir=${CONTAINER_WORKDIR} -t \
	${IMAGE_NAME}:${VERSION} bash -lc "git config --global --add safe.directory /galois && make hooks && make pre-commit"
