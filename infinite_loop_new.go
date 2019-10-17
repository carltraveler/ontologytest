/*
 * Copyright (C) 2018 The ontology Authors
 * This file is part of The ontology library.
 *
 * The ontology is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * The ontology is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with The ontology.  If not, see <http://www.gnu.org/licenses/>.
 */

package main

import (
	"fmt"
	"math"
	"time"
	//"github.com/ontio/ontology/common"
	"github.com/ontio/ontology/core/types"
	sc "github.com/ontio/ontology/smartcontract"
	"io/ioutil"
	//"os"
	//"strings"
)

func main() {
	codeFile := "re_regular.avm"

	codeStr, err := ioutil.ReadFile(codeFile)
	if err != nil {
		fmt.Printf("Read %s Error.\n", codeFile)
		print("xxxxxxxxxxxxxx\n")
		//return nil
		return
	}
	evilBytecode := codeStr

	config := &sc.Config{
		Time:   10,
		Height: 100000000,
		Tx:     &types.Transaction{},
	}
	//cache := storage.NewCloneCache(testBatch)
	sct := sc.SmartContract{
		Config:  config,
		Gas:     math.MaxUint64,
		CacheDB: nil,
	}
	engine, err := sct.NewExecuteEngine(evilBytecode, types.InvokeNeo)
	if err != nil {
		//t.Fatal(err)
		print("ERROR runned\n")
	}
	start := time.Now()
	_, err = engine.Invoke()

	if err != nil {
		//t.Fatal(err)
		fmt.Printf("ERROR runned: %s\n", err)
		return
	}
	end := time.Now()
	timeresult := end.Sub(start)
	fmt.Printf("Conmsumed time: %s\n", timeresult)

	cgas := math.MaxUint64 - sct.Gas
	fmt.Printf("Conmsumed gas : %d\n", cgas)

	print("all done\n")
}
