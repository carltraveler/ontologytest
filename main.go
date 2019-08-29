/*
 * Copyright (C) 2019 The ontology Authors
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
	"bufio"
	"encoding/csv"
	"encoding/hex"
	"fmt"
	"io"
	"io/ioutil"
	"math"
	"os"
	"runtime"
	"strings"
	"time"

	"github.com/ontio/ontology/account"
	"github.com/ontio/ontology/cmd"
	"github.com/ontio/ontology/cmd/utils"
	"github.com/ontio/ontology/common/config"
	"github.com/ontio/ontology/common/log"
	"github.com/ontio/ontology/core/payload"
	"github.com/ontio/ontology/core/store/ledgerstore"
	"github.com/ontio/ontology/core/store/overlaydb"
	"github.com/ontio/ontology/core/types"
	common2 "github.com/ontio/ontology/http/base/common"
	"github.com/ontio/ontology/smartcontract"
	"github.com/ontio/ontology/smartcontract/event"
	"github.com/ontio/ontology/smartcontract/service/neovm"
	"github.com/ontio/ontology/smartcontract/service/wasmvm"
	"github.com/ontio/ontology/smartcontract/storage"
	neotype "github.com/ontio/ontology/vm/neovm/types"
	"github.com/urfave/cli"
)

const (
	DEFAULT_BYTECODE    = "./test.avm.str"
	DEFAULT_TESTCASE    = "./testcases.txt"
	DEFAULT_WALLET      = "./wallet.dat"
	DEFAULT_LEDGER_PATH = "./Chain"
)

var (
	//nvm-tool setting
	NvmByteCodeFlag = cli.StringFlag{
		Name:  "bytecode,b",
		Usage: "smart contract bytecode.",
		Value: DEFAULT_BYTECODE,
	}
	TestCasesFlag = cli.StringFlag{
		Name:  "testcases,t",
		Usage: "test cases",
		Value: DEFAULT_TESTCASE,
	}
	LedgerPathFlag = cli.StringFlag{
		Name:  "ledger,l",
		Usage: "ledger path",
		Value: DEFAULT_LEDGER_PATH,
	}
	VMTypeFlag = cli.BoolFlag{
		Name:  "type,p",
		Usage: "type p",
	}
	CallVmType byte = payload.NEOVM_TYPE
)

func setupAPP() *cli.App {
	app := cli.NewApp()
	app.Usage = "NeoVM CLI"
	app.Action = neovmCLI
	app.Version = config.Version
	app.Copyright = "Copyright in 2018 The Ontology Authors"
	app.Flags = []cli.Flag{
		NvmByteCodeFlag,
		TestCasesFlag,
		LedgerPathFlag,
		VMTypeFlag,
		//WalletFlag,
	}
	app.Before = func(context *cli.Context) error {
		runtime.GOMAXPROCS(runtime.NumCPU())
		return nil
	}
	return app
}

func main() {
	if err := setupAPP().Run(os.Args); err != nil {
		cmd.PrintErrorMsg(err.Error())
		os.Exit(1)
	}
}

func neovmCLI(ctx *cli.Context) {
	// assert the vm type
	isNeoType := ctx.GlobalBool(utils.GetFlagName(VMTypeFlag))
	if isNeoType {
		CallVmType = payload.NEOVM_TYPE
		fmt.Printf("Neo VM\n")
	} else {
		CallVmType = payload.WASMVM_TYPE
		fmt.Printf("WASM VM\n")
	}
	// create account
	owner := account.NewAccount("")

	// read nvm bytecode
	codeFile := ctx.String(utils.GetFlagName(NvmByteCodeFlag))
	codeStr, err := ioutil.ReadFile(codeFile)
	if err != nil {
		log.Errorf("open nvm code file failed: %s", err)
		return
	}
	code, err := hex.DecodeString(strings.TrimSpace(string(codeStr)))
	if err != nil {
		log.Errorf("failed to decode code from hex to binary: %s", err)
		return
	}

	gaslimit := uint64(100000000)
	mtx := utils.NewDeployCodeTransaction(0, gaslimit, code, CallVmType, "test", "test", "test", "test", "test")
	d := mtx.Payload.(*payload.DeployCode)
	if d == nil {
		log.Errorf("failed to get smart contract deploy address")
		return
	}
	contractAddr := d.Address()

	// init ledger
	ledgerDir := ctx.String(utils.GetFlagName(LedgerPathFlag))
	dbPath := fmt.Sprintf("%s%s%s", ledgerDir, string(os.PathSeparator), ledgerstore.DBDirState)
	merklePath := fmt.Sprintf("%s%s%s", ledgerDir, string(os.PathSeparator), ledgerstore.MerkleTreeStorePath)
	stateStore, err := ledgerstore.NewStateStore(dbPath, merklePath, 0)
	if err != nil {
		log.Errorf("failed to create state store: %s", err)
		return
	}
	overlay := stateStore.NewOverlayDB()

	// deploy nvm byte code
	if err := executeDeployTx(stateStore, overlay, owner, mtx); err != nil {
		log.Errorf("failed to deploy smart contract: %s", err)
		return
	}
	log.Infof("deploy %s done, address = %s", codeFile, contractAddr.ToHexString())

	// load testcases
	testcaseFile := ctx.String(utils.GetFlagName(TestCasesFlag))
	f, err := os.Open(testcaseFile)
	if err != nil {
		log.Errorf("failed to read testcase file %s: %s", testcaseFile, err)
		return
	}
	defer f.Close()

	reader := csv.NewReader(bufio.NewReader(f))
	gas_sum := uint64(0)
	case_id := 1
	for {
		line, err := reader.Read()
		if err == io.EOF {
			break
		} else if err != nil {
			log.Errorf("read testcase file: %s", err)
			return
		}

		if len(line) != 3 {
			log.Errorf("bad testcase: %v", line)
			continue
		}

		pattern := strings.TrimSpace(line[0])
		text := strings.TrimSpace(line[1])
		result := strings.TrimSpace(line[2])

		var mtx *types.MutableTransaction
		//params := []interface{}{"init"}
		if CallVmType == payload.NEOVM_TYPE {
			params := []interface{}{"match", []interface{}{pattern, text}}
			mtx, err = common2.NewNeovmInvokeTransaction(0, gaslimit, contractAddr, params)
		} else if CallVmType == payload.WASMVM_TYPE {
			params := []interface{}{"match", pattern, text}
			mtx, err = common2.NewWasmVMInvokeTransaction(0, gaslimit, contractAddr, params)
		} else {
			log.Errorf("VM type error")
		}

		if err != nil {
			log.Errorf("create tx for testcase %d failed: %s", case_id, err)
			break
		}
		testResult, gas, err := executeInvokeTx(stateStore, overlay, owner, mtx)
		if err != nil {
			log.Errorf("process testcase %d failed: %s", case_id, err)
			break
		}
		if testResult != result {
			log.Errorf("testcase %d failed: '%s' vs '%s'", case_id, testResult, result)
			break
		}

		//log.Infof("case %d: passed", case_id)
		gas_sum += gas
		case_id++
	}
	log.Infof("sum gas: %d", gas_sum)
}

func executeDeployTx(store *ledgerstore.StateStore, overlay *overlaydb.OverlayDB, user *account.Account, mtx *types.MutableTransaction) error {
	cache := storage.NewCacheDB(overlay)

	if err := utils.SignTransaction(user, mtx); err != nil {
		return fmt.Errorf("sign deploy: %s", err)
	}
	tx, err := mtx.IntoImmutable()
	if err != nil {
		return fmt.Errorf("deploy tx immu: %s", err)
	}

	gasTable := make(map[string]uint64)
	neovm.GAS_TABLE.Range(func(k, value interface{}) bool {
		key := k.(string)
		val := value.(uint64)
		gasTable[key] = val

		return true
	})

	notify := &event.ExecuteNotify{TxHash: tx.Hash(), State: event.CONTRACT_STATE_FAIL}
	if err := store.HandleDeployTransaction(nil, overlay, gasTable, cache, tx, nil, notify); err != nil {
		return fmt.Errorf("handle deploy tx: %s", err)
	}
	cache.Commit()
	return nil
}

func executeInvokeTx(store *ledgerstore.StateStore, overlay *overlaydb.OverlayDB, user *account.Account, mtx *types.MutableTransaction) (string, uint64, error) {
	if err := utils.SignTransaction(user, mtx); err != nil {
		return "", 0, fmt.Errorf("failed to sign tx: %s", err)
	}
	tx, err := mtx.IntoImmutable()
	if err != nil {
		return "", 0, fmt.Errorf("failed to invoke tx immu: %s", err)
	}

	cache := storage.NewCacheDB(overlay)
	config := &smartcontract.Config{
		Time:   uint32(time.Now().Unix()),
		Height: 1000,
		Tx:     tx,
	}
	invoke := tx.Payload.(*payload.InvokeCode)

	sc := smartcontract.SmartContract{
		Config:  config,
		Store:   nil,
		CacheDB: cache,
		Gas:     math.MaxUint64,
		//PreExec: true,
	}

	//start the smart contract executive function
	engine, err := sc.NewExecuteEngine(invoke.Code, tx.TxType)
	if err != nil {
		return "", 0, fmt.Errorf("start exec engine failed: %s", err)
	}
	result, err := engine.Invoke()
	if err != nil {
		return "", 0, fmt.Errorf("preexec invoke failed: %s", err)
	}
	//for _, n := range sc.Notifications {
	//	log.Infof(" '%v' : '%v'", n.ContractAddress, n.States)
	//}

	if tx.TxType == types.InvokeNeo {
		cv, err := result.(*neotype.VmValue).ConvertNeoVmValueHexString()
		if err != nil {
			return "", 0, fmt.Errorf("preexec invoke failed to convert result")
		}

		if cv == "01" {
			return "TRUE", math.MaxUint64 - sc.Gas, nil
		} else if cv == "00" {
			return "FALSE", math.MaxUint64 - sc.Gas, nil
		} else {
			return cv.(string), math.MaxUint64 - sc.Gas, nil
			//return cv.(string), 0, fmt.Errorf("preexec invoke failed to convert result")
		}
	} else if tx.TxType == types.InvokeWasm {
		sc.Gas = engine.(*wasmvm.WasmVmService).GasLimit
		if result.([]byte)[0] == 0 {
			return "FALSE", math.MaxUint64 - sc.Gas, nil
		} else if result.([]byte)[0] == 1 {
			return "TRUE", math.MaxUint64 - sc.Gas, nil
		} else {
			return "", math.MaxUint64 - sc.Gas, nil
		}
	} else {
		return "", 0, fmt.Errorf("preexec invoke failed: %s", err)
	}

	//return cv.(string), math.MaxUint64 - sc.Gas, nil
}
