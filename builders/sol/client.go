/*
 * This file is part of the Soletta Project
 *
 * Copyright (C) 2015 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

package main

import (
	"archive/tar"
	"bytes"
	"flag"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"
)

var targetPlatform = ""
var targetPlats []string
var targetTags []string

func runClient() {
	if flag.NArg() == 0 {
		writeList(os.Stdout)
		return
	}

	// TODO: Think about whether make sense to cache this.
	availableTargets := getList()

	var candidates []string
	for _, arg := range flag.Args() {
		switch {
		case strings.HasPrefix(arg, "platform-"):
			candidates = append(candidates, arg[9:])
		case isTag(arg):
			targetTags = append(targetTags, arg)
		default:
			for _, target := range availableTargets {
				if strings.Contains(target, arg) {
					candidates = append(candidates, target[9:])
				}
			}
		}
	}

	switch len(candidates) {
	case 0:
		log.Fatal("Must pass a platform as argument")
	case 1:
		targetPlatform = candidates[0]
	default:
		log.Fatal("Multiple platforms set in the command line: ", candidates)
	}

	targetPlats = strings.Split(targetPlatform, "-")

	fmt.Println("target platform:", targetPlatform)
	fmt.Println("target tags:", targetTags)

	runBuild()
}

func getList() []string {
	var buf bytes.Buffer
	writeList(&buf)

	var result []string
	for _, s := range strings.Split(buf.String(), "\n") {
		s = strings.Trim(s, " ")
		if s != "" {
			result = append(result, s)
		}
	}

	return result
}

func writeList(w io.Writer) {
	resp, err := http.Get("http://" + *addr + "/list")
	if err != nil {
		log.Fatal(err)
	}
	defer resp.Body.Close()

	io.Copy(w, resp.Body)
}

func writeArchive(files []string, w io.Writer) error {
	tw := tar.NewWriter(w)

	for _, name := range files {
		f, err := os.Open(name)
		if err != nil {
			log.Fatal(err)
		}
		fi, err := f.Stat()
		if err != nil {
			log.Fatal(err)
		}

		header, _ := tar.FileInfoHeader(fi, "")
		header.Name = name
		tw.WriteHeader(header)
		io.Copy(tw, f)
	}

	tw.Close()
	return nil
}

func isTag(name string) bool {
	return strings.HasPrefix(name, "tag-")
}

func contains(needle string, haystack []string) bool {
	for _, s := range haystack {
		if s == needle {
			return true
		}
	}
	return false
}

func pickFiles(tags []string) []string {
	var files []string

	walkFunc := func(path string, info os.FileInfo, err error) error {
		rel, _ := filepath.Rel(initialDir, path)
		if strings.HasPrefix(rel, ".") || strings.HasPrefix(rel, "out") {
			return nil
		}

		if strings.HasPrefix(rel, "Makefile") {
			fmt.Println("Ignoring", rel)
			return nil
		}

		base := info.Name()

		if info.IsDir() {
			if isTag(base) {
				if !contains(base, tags) {
					return filepath.SkipDir
				}

			} else if strings.HasPrefix(base, "plat-") {
				plats := strings.Split(base[5:], "-")
				for _, p := range plats {
					if !contains(p, targetPlats) {
						return filepath.SkipDir
					}
				}
			}
		}

		files = append(files, rel)
		return nil
	}

	filepath.Walk(initialDir, walkFunc)
	return files
}

func runBuild() {
	files := pickFiles(targetTags)

	fmt.Println("Files picked:", files)

	var buf bytes.Buffer
	err := writeArchive(files, &buf)
	if err != nil {
		log.Fatal("Couldn't archive the files")
	}

	// Send platform as part of the request!
	resp, err := http.Post("http://"+*addr+"/build/platform-"+targetPlatform, "application/octet-stream", &buf)
	if err != nil {
		log.Fatal(err)
	}
	defer resp.Body.Close()

	contentType := resp.Header["Content-Type"][0]

	if strings.HasPrefix(contentType, "text/plain") {
		io.Copy(os.Stdout, resp.Body)
		os.Exit(1)
	} else if contentType == "application/octet-stream" {
		os.MkdirAll("out", 0755)
		bin, err := os.Create("out/" + sanitizePlatformName(targetPlatform) + ".zip")
		if err != nil {
			log.Fatal(err)
		}

		c := make(chan struct{})
		go func() {
			io.Copy(bin, resp.Body)
			close(c)
		}()

		done := false
		for {
			select {
			case <-c:
				fmt.Println("DONE")
				done = true
			case <-time.After(3 * time.Second):
				stat, _ := bin.Stat()
				fmt.Printf("%.2f%%\n", (float64(stat.Size())/float64(resp.ContentLength))*100)
			}
			if done {
				break
			}
		}

		stat, _ := bin.Stat()
		fmt.Println("Written", bin.Name(), "with", stat.Size(), "bytes")
		bin.Close()
	} else {
		fmt.Println("Unknown Content-Type in response", contentType)
	}
}
