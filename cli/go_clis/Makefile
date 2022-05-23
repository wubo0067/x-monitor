VERSION ?= v0.0.1

GIT_COMMIT_HASH ?= $(shell git rev-parse --short HEAD)
GIT_VERSION ?= $(shell git describe --tags --dirty)

BUILD_TIME ?= $(shell date -u '+%Y-%m-%d_%H:%M:%S')

WORKDIR=$(shell pwd)
ADDX=$(shell sh -c chmod +x ./hack/*)
PACKAGES=`go list ./... | grep -v /vendor/`
GOFILES=$(shell find . -name "*.go" -type f -not -path "./vendor/*")

GOARCH ?= $(shell go env GOARCH)
GOOS ?= $(shell go env GOOS)
GO_VERSION := $(shell go version)

# Set your version by env or using latest tags from git
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

.PHONY: all clean snoop_socket
.IGNORE : lint

all: snoop_socket

snoop_socket:
	go generate -v -x ./...
	gofmt -s -w $(GOFILES)
	GOOS=linux GOARCH=amd64 go build -mod=vendor \
		-ldflags '-linkmode external -extldflags -static' -gcflags 'all=-N -l' -race  -o bin/snoop_socket ./snoop_socket

clean:
	-rm bin/snoop_socket