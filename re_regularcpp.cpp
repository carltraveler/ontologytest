//#define WASM_LOCAL_DEBUG 
//#define WASM_TEST
#include<ontiolib/ontio.hpp>

using namespace ontio;
using std::string;
using std::vector;
using std::map;
typedef uint8_t INST;

class hello: public contract {
	private:
		key filetextkey = make_key("myfiletextkey");
		vector<INST> nfa_instr_all;

		INST CHR = 0x00;
		INST ANY = 0x01;
		INST CCL = 0x02;
		INST BOL = 0x03;
		INST EOL = 0x04;
		INST COL = 0x05;
		INST END = 0x06;
		INST REPEATSTAR = 0x7;
		INST REPEATPLUS = 0x8;
		INST REPEATNO   = 0x9;
		INST NOP = 0xa;
		uint128_t bitset = 0;

		map<INST, string> nfa_map = {{CHR,"CHR"}, {ANY, "ANY"}, {CCL, "CCL"}, {BOL, "BOL"}, {EOL, "EOL"}, {COL, "COL"}, {END, "END"}, {REPEATSTAR, "REPEATSTAR"},{REPEATPLUS, "REPEATPLUS"}, {REPEATNO, "REPEATNO"}, {NOP, "NOP"}};

	public:
		using contract::contract;

		uint8_t match(string &pattern, string &oris) {
			nfa_instr_all.clear();
			re_compile(pattern);
			nfa_dump(nfa_instr_all);
			if (re_exec(oris))
				return 1;
			else
				return 0;
		}

		uint8_t putext(string &filetext) {
			//printf("%d\n", filetext.size());
			storage_put(filetextkey, filetext);
			return 1;
		}

		vector<string> split(const std::string &str, char c) {
			vector<string> res;
			uint32_t len_t = str.size();
			uint32_t i = 0;
			string t("");

			for(i = 0; i < len_t; i++) {
				char x = str[i];
				if (x == ' ') {
					continue;
				} else if (x != c) {
					t.push_back(x);
				} else {
					if (t != "") {
						res.push_back(t);
					} else {
						res.push_back("");
					}
					t = "";
				}
			}

			res.push_back(t);
			return res;
		}

		uint8_t performancematch(uint32_t count) {
			string filetext;

			check(storage_get(filetextkey,filetext), "get filetext noting\n");
			check(filetext.size() != 0, "filetext size zero\n");

			auto filetextarray = split(filetext,'\n');
			check(filetextarray.size() != 0 , "filetextarray empty\n");
			//printf("filetext.size %d\n", filetext.size());

			uint32_t i = 0;
			while ( i < count ) {
				for (auto line : filetextarray) {
					//printf("%s\n", line.c_str());
					auto linearray = split(line, ',');
					check(linearray.size() == 3, "linearray len not 3");

					string pattern = linearray[0];
					string text = linearray[1];
					string result  = linearray[2];
					bool lres = false;
					//printf("%s,", pattern.c_str());
					//printf("%s,\n", text.c_str());
					//printf("%s,", result.c_str());

					if (result.substr(0, 4) == "TRUE") {
						lres = true;
					} else if (result.substr(0, 5) == "FALSE") {
						lres = false;
					} else {
						check(false, "result expect error 00");
					}

					bool rres = match(pattern, text);
					check(rres == lres , "result expect error 11");
				}

				i++;
			}

			return 1;
		}
	private:

		void re_compile(string &pattern) {
			uint32_t i = 0;
			uint32_t l = pattern.size();
			uint32_t colosure_pc = 0;
			bool next_char_normal = false;
			uint8_t p_char;

			while (i <  pattern.size()) {
				p_char = pattern[i];
				if (next_char_normal == true) {
					next_char_normal = false;
					colosure_pc = nfa_instr_all.size();
					store(NOP);
					store(CHR);
					store(p_char);
					i += 1;
					continue;
				}

				if (p_char == '.') {
					colosure_pc = nfa_instr_all.size();
					store(NOP);
					store(ANY);
				} else if (p_char == '^') {
					if (nfa_instr_all.size() == 0) {
						store(BOL);
					} else {
						colosure_pc = nfa_instr_all.size();
						store(NOP);
						store(CHR);
						store(p_char);
					}
				} else if (p_char == '$') {
					if (i == l - 1) {
						store(EOL);
					} else {
						colosure_pc = nfa_instr_all.size();
						store(NOP);
						store(CHR);
						store(p_char);
					}
				} else if  (p_char == '[') {
					bool need_reverse = false;
					bitset = 0;
					colosure_pc = nfa_instr_all.size();
					store(NOP);
					store(CCL);
					i += 1;
					p_char = pattern[i];
					if (p_char == '^') {
						need_reverse = true;
						i += 1;
						p_char = pattern[i];
					} else 
						need_reverse = false;

					if (p_char == '-') {
						setbit(p_char);
						i += 1;
					}

					p_char = pattern[i];
					next_char_normal = false;
					while ((i < pattern.size()) and ((p_char != ']') or (next_char_normal == true))) {
						if (next_char_normal == true) {
							setbit(pattern[i]);
							next_char_normal = false;
							i += 1;
							p_char = pattern[i];
							continue;
						}

						next_char_normal = false;
						if (p_char == '\\') {
							next_char_normal = true;
						} else if (p_char == '-') {
							uint8_t c1 = pattern[i-1];
							uint8_t c2 = 0;
							if (pattern[i+1] != ']') {
								c2 = pattern[i+1];
							}else {
								c2 = 127;
							}

							while (c1 <= c2) {
								setbit(c1);
								c1 += 1;
							}
						} else
							setbit(pattern[i]);
						i+= 1;
						p_char = pattern[i];
					}

					if (p_char != ']') {
						check(false, "miss ]");
						return;
					}

					if (need_reverse) {
						xormask();
					}

					storebitset(bitset);
				} else if (p_char == '*') {
					setpc(colosure_pc, REPEATSTAR);
					store(END);
				} else if (p_char == '+') {
					setpc(colosure_pc, REPEATPLUS);
					store(END);
				} else if (p_char == '?') {
					setpc(colosure_pc, REPEATNO);
					store(END);
				} else if (p_char == '\\') {
					next_char_normal = true;
				} else {
					colosure_pc = nfa_instr_all.size();
					store(NOP);
					store(CHR);
					store(p_char);
				} 

				i+= 1;
			}
		}

		bool re_exec(string &oristring) {
			INST start_instr = nfa_instr_all[0];
			if (start_instr == BOL) {
				return re_match(oristring, nfa_instr_all);
			} else if (start_instr == CHR) {
				uint8_t start_char = nfa_instr_all[1];
				uint32_t j = 0;
				while(j < oristring.size()) {
					string ss = oristring.substr(j);
					uint32_t i = 0;
					while (i < ss.size() and start_char !=ss[i])
						i += 1;

					if (i < ss.size())
						if (re_match(ss.substr(i), nfa_instr_all))
							return true;
					j = j + i + 1;
				}
				return false;
			} else {
				uint32_t j = 0;
				while (not  re_match(oristring.substr(j), nfa_instr_all)) {
					j += 1;
					if (j >= oristring.size())
						return false;
				}
			}
			return true;
		}

		bool re_match(const string &oristring, const vector<INST> &nfa_instr) {
			if (nfa_instr.size() == 0)
				return true;
			uint32_t si = 0, pc = 0;
			INST closure_count = NOP;

			while (si <= oristring.size() and pc < nfa_instr.size()) {
				bool check_repeat = false;
				INST opcode = nfa_instr[pc];

				if (opcode == EOL) {
					if (si != oristring.size() and oristring.size() != 0)
						return false;
					check(pc == nfa_instr.size() - 1, "re_match error");
					closure_count = NOP;
				} else if (opcode == NOP)
					closure_count = NOP;
				else if (opcode == END)
					 closure_count = NOP;
				else if (opcode == CHR) {
					pc += 1;
					if (oristring.substr(si) == "" or oristring[si] !=  nfa_instr[pc]) {
						if (closure_count == NOP)
							return false;
						else if (closure_count == REPEATSTAR)
							uint32_t aaa = 0;
						else if (closure_count == REPEATPLUS)
							return false;
						else if (closure_count == REPEATNO)
							uint32_t aaa = 0;
						else
							check(false, "re_match error");
					} else {
						uint8_t match_c = oristring[si];
						if (closure_count == NOP)
							si += 1;
						else if (closure_count == REPEATNO) {
							closure_count = NOP;
							// re_match zero try
							auto v0 = vector<INST>(nfa_instr.begin()+pc+2, nfa_instr.end());
							if (re_match(oristring.substr(si), v0))
								return true;
							si += 1;
						}
						else if (closure_count == REPEATSTAR){
							// from re_match zero try
							while (true){
								if (si >= oristring.size() or (oristring[si] != match_c))
									break;
								closure_count = NOP;
								auto v0 = vector<INST>(nfa_instr.begin()+pc+2, nfa_instr.end());
								if (re_match(oristring.substr(si), v0))
									return true;
								si += 1;
							}
						} else {
							si += 1;
							while (true) {
								if (si >= oristring.size() or oristring[si] != match_c)
									break;
								closure_count = NOP;
								auto v0 = vector<INST>(nfa_instr.begin()+pc+2, nfa_instr.end());
								if (re_match(oristring.substr(si), v0))
									return true;
								si += 1;
							}
						}
					}
					closure_count = NOP;
				} else if (opcode == ANY) {
					if (oristring.substr(si) == "") {
						if (closure_count == NOP)
							return false;
						else if (closure_count == REPEATSTAR){
							uint32_t aaa = 0;
						}
						else if (closure_count == REPEATPLUS)
							return false;
						else if (closure_count == REPEATNO) {
							uint32_t aaa = 0;
						}
						else
							check(false, "re_match error 00");
					} else {
						uint8_t match_c = oristring[si];
						if (closure_count == NOP)
							si += 1;
						else if (closure_count == REPEATNO) {
							closure_count = NOP;
							// re_match zero try
							auto v0 = vector<INST>(nfa_instr.begin()+pc+2, nfa_instr.end());
							if (re_match(oristring.substr(si), v0))
								return true;
							si += 1;
						}
						else if (closure_count == REPEATSTAR){
							// from re_match zero try
							while (true){
								if (si >= oristring.size())
									break;
								closure_count = NOP;
								auto v0 = vector<INST>(nfa_instr.begin()+pc+2, nfa_instr.end());
								if (re_match(oristring.substr(si), v0))
									return true;
								si += 1;
							}
						} else {
							si += 1;
							while (true) {
								if (si >= oristring.size())
									break;
								closure_count = NOP;
								auto v0 = vector<INST>(nfa_instr.begin()+pc+2, nfa_instr.end());
								if (re_match(oristring.substr(si), v0))
									return true;
								si += 1;
							}
						}
					}
					closure_count = NOP;
				} else if (opcode == BOL) {
					if (si != 0)
						return false;
					closure_count = NOP;
				} else if (opcode == CCL) {
					pc += 1;
					if (oristring.substr(si) == "" or (not isetbit(oristring[si], getbitsetlittlendian(nfa_instr,pc)))) {
						if (closure_count == NOP)
							return false;
						else if (closure_count == REPEATSTAR){
							uint32_t aaa = 0;
						}
						else if (closure_count == REPEATPLUS)
							return false;
						else if (closure_count == REPEATNO) {
							uint32_t aaa = 0;
						}
						else
							check(false, "re_match error 00");
					} else {
						if (closure_count == NOP)
							si += 1;
						else if (closure_count == REPEATNO) {
							closure_count = NOP;
							// re_match zero try
							auto v0 = vector<INST>(nfa_instr.begin()+pc+sizeof(uint128_t) + 1, nfa_instr.end());
							if (re_match(oristring.substr(si), v0))
								return true;
							si += 1;
						}
						else if (closure_count == REPEATSTAR){
							// from re_match zero try
							while (true){
								if (si >= oristring.size() or (not isetbit(oristring[si], getbitsetlittlendian(nfa_instr,pc))))
									break;
								closure_count = NOP;
								auto v0 = vector<INST>(nfa_instr.begin()+pc+sizeof(uint128_t) + 1, nfa_instr.end());
								if (re_match(oristring.substr(si), v0))
									return true;
								si += 1;
							}
						} else {
							si += 1;
							while (true) {
								if (si >= oristring.size() or (not isetbit(oristring[si], getbitsetlittlendian(nfa_instr,pc))))
									break;
								closure_count = NOP;
								auto v0 = vector<INST>(nfa_instr.begin()+pc+sizeof(uint128_t) + 1, nfa_instr.end());
								if (re_match(oristring.substr(si), v0))
									return true;
								si += 1;
							}
						}
					}
					closure_count = NOP;
				} else if (opcode == REPEATSTAR) {
					closure_count = REPEATSTAR;
				} else if (opcode == REPEATNO) {
					closure_count = REPEATNO;
				} else if (opcode == REPEATPLUS) {
					closure_count = REPEATPLUS;
				} else {
					check(false, "re_match fault opcode");
				}

				if (opcode != CCL)
					pc += 1;
				else
					pc += sizeof(uint128_t);
			}

			if (pc == nfa_instr.size())
				return true;
			else
				return false;
		}

		void store(INST instr) {
			nfa_instr_all.push_back(instr);
		}

		void storebitset(uint128_t &bitset) {
			nfa_instr_all.push_back(uint8_t(bitset));
			nfa_instr_all.push_back(uint8_t(bitset>>8));
			nfa_instr_all.push_back(uint8_t(bitset>>16));
			nfa_instr_all.push_back(uint8_t(bitset>>24));
			nfa_instr_all.push_back(uint8_t(bitset>>32));
			nfa_instr_all.push_back(uint8_t(bitset>>40));
			nfa_instr_all.push_back(uint8_t(bitset>>48));
			nfa_instr_all.push_back(uint8_t(bitset>>56));
			nfa_instr_all.push_back(uint8_t(bitset>>64));
			nfa_instr_all.push_back(uint8_t(bitset>>72));
			nfa_instr_all.push_back(uint8_t(bitset>>80));
			nfa_instr_all.push_back(uint8_t(bitset>>88));
			nfa_instr_all.push_back(uint8_t(bitset>>96));
			nfa_instr_all.push_back(uint8_t(bitset>>104));
			nfa_instr_all.push_back(uint8_t(bitset>>112));
			nfa_instr_all.push_back(uint8_t(bitset>>120));
		}

		uint128_t getbitsetlittlendian(const vector<INST> &nfa_instr, uint32_t pc) {
			return uint128_t(nfa_instr[pc+0])<< 0 |\
				   uint128_t(nfa_instr[pc+1])<< 8 |\
				   uint128_t(nfa_instr[pc+2])<< 16 |\
				   uint128_t(nfa_instr[pc+3])<< 24 |\
				   uint128_t(nfa_instr[pc+4])<< 32 |\
				   uint128_t(nfa_instr[pc+5])<< 40 |\
				   uint128_t(nfa_instr[pc+6])<< 48 |\
				   uint128_t(nfa_instr[pc+7])<< 56 |\
				   uint128_t(nfa_instr[pc+8])<< 64 |\
				   uint128_t(nfa_instr[pc+9])<< 72 |\
				   uint128_t(nfa_instr[pc+10]) << 80 |
				   uint128_t(nfa_instr[pc+11]) << 88 |
				   uint128_t(nfa_instr[pc+12]) << 96 |\
				   uint128_t(nfa_instr[pc+13]) << 104 |\
				   uint128_t(nfa_instr[pc+14]) << 112|\
				   uint128_t(nfa_instr[pc+15]) << 120;
		}

		void setbit(uint8_t p_char) {
			check(p_char < 127, "p_char out of size");
			bitset |= ((uint128_t(1)) << p_char);
		}

		bool isetbit(uint8_t p_char, const uint128_t &bitset) {
			check(p_char < 127, "p_char out of size");
			uint8_t isset = ((bitset >> p_char) & 0x1);
			//if (isset)
			//	printf("set char %c\n",p_char);
			//else
			//	printf("non set  %c\n",p_char);
			return isset != 0;
		}

		void xormask() {
			bitset = ~bitset;
		}

		void setpc(uint32_t pc, INST inst) {
			check(pc < nfa_instr_all.size(), "over size");
			nfa_instr_all[pc] = inst;
		}

		string nfa_dump(vector<INST> &nfa_instr) {
			uint32_t pc = 0;
			string inst_string;
			while (pc < nfa_instr.size()) {
				auto tst = nfa_map.find(nfa_instr[pc]);
				inst_string = tst->second;
				if (nfa_instr[pc] == CHR) {
					pc += 1;
					INST test = nfa_instr[pc];
				}else if (nfa_instr[pc] == CCL) {
					pc += 16; //skip bitset
				}
				pc += 1;
			}

			return inst_string;
		}
};

ONTIO_DISPATCH( hello, (match)(performancematch)(putext))

	/*
extern "C" void invoke() {
	string action("test_regular");
	auto v = pack(action);

	save_input_arg(v.data(), v.size());
	apply();
}
*/
