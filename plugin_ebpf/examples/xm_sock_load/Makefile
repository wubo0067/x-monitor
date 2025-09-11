TARGET := xm_sock_load
VERSION ?= v0.0.1

GIT_COMMIT_HASH ?= $(shell git rev-parse --short HEAD)
GIT_VERSION ?= $(shell git describe --tags --always)

BUILD_TIME ?= $(shell date -u '+%Y-%m-%d_%H:%M:%S')

WORKDIR=$(shell pwd)
ADDX=$(shell sh -c chmod +x ./hack/*)
PACKAGES=`go list ./... | grep -v /vendor/`
GOFILES=$(shell find . -name "*.go" -type f -not -path "./vendor/*")

GOARCH ?= $(shell go env GOARCH)
GOOS ?= $(shell go env GOOS)
GO_VERSION := $(shell go version)

VERSION?=""
ifeq ($(VERSION), "")
    LATEST_TAG=$(shell git describe --tags)
    ifeq ($(LATEST_TAG),)
        # Forked repo may not sync tags from upstream, so give it a default tag to make CI happy.
        VERSION="unknown"
    else
        VERSION=$(LATEST_TAG)
    endif
endif

.PHONY: all build image clean lint test
.IGNORE : lint

all: build

build:
	go generate -v -x ./...
	gofmt -s -w $(GOFILES)
	goimports -v -w $(GOFILES)
	GOOS=linux GOARCH=amd64 go build -mod=vendor \
		-ldflags '-X main.Release=$(VERSION) -X main.Commit=$(GIT_COMMIT_HASH) -X main.BuildTime=$(BUILD_TIME) -linkmode external' \
		-gcflags 'all=-N -l' -race  \
		-o ./$(TARGET) main.go xmcgsockprog_bpfel.go

clean:
	-rm ../../../bin/$(TARGET)
	-rm *.o
	-rm *_bpf*.go