#if SMALL_ACCESS

//SmallAccessFtl.cpp
//class file for ftl
//
#include "SmallAccessFtl.h"
#include "ChannelPacket.h"
#include <cmath>

using namespace NVDSim;
using namespace std;

SmallAccessFtl::SmallAccessFtl(Controller *c){
	int numBlocks = NUM_PACKAGES * DIES_PER_PACKAGE * PLANES_PER_DIE * BLOCKS_PER_PLANE;

	offset = log2(NV_PAGE_SIZE * 1024);
	wordBitWidth = log2(WORDS_PER_PAGE);
	pageBitWidth = log2(PAGES_PER_BLOCK);
	blockBitWidth = log2(BLOCKS_PER_PLANE);
	planeBitWidth = log2(PLANES_PER_DIE);
	dieBitWidth = log2(DIES_PER_PACKAGE);
	packageBitWidth = log2(NUM_PACKAGES);

	channel = 0;
	die = 0;
	plane = 0;
	lookupCounter = 0;

	busy = 0;

	addressMap = std::unordered_map<uint64_t, uint64_t>();

	// for NAND flash last vector will only ever contain one value
#if !PCM
	dirty = vector<vector<vector<bool>>>(numBlocks, vector<vector<bool>>(PAGES_PER_BLOCK, vector<bool>(WORDS_PER_PAGE, false)));
#endif

        used = vector<vector<vector<bool>>>(numBlocks, vector<vector<bool>>(PAGES_PER_BLOCK, vector<bool>(WORDS_PER_PAGE, false)));
	
	transactionQueue = list<FlashTransaction>();

	used_page_count = 0;

	controller = c;
}

ChannelPacket *SmallAccessFtl::translate(ChannelPacketType type, uint64_t addr){
  uint package, die, plane, block, page, word, size;
  uint64_t tempA, tempB, physicalAddress = addressMap[addr];

	if (physicalAddress > TOTAL_SIZE*1024 - 1 || physicalAddress < 0){
		ERROR("Inavlid address in Ftl: "<<physicalAddress);
		exit(1);
	}


	// if we're using a memory other than nand, we will have word granularity available
	if(DEVICE_TYPE != "NAND" || (DEVICE_TYPE == "NOR" && type == WRITE)){
	        offset = log2(NV_WORD_SIZE);
		//cout<<"Word size is "<<NV_WORD_SIZE<<endl;
		//cout<<"offset is "<<offset<<endl;
		//cout<<"physical address before offset "<<physicalAddress<<endl;
		physicalAddress = physicalAddress >> offset;
		//cout<<"physical address after offset "<<physicalAddress<<endl;
	        tempA = physicalAddress;
		//cout<<"tempA we're oring with "<<tempA<<endl;
		physicalAddress = physicalAddress >> wordBitWidth;
		//cout<<"the physical address after shifting was "<<physicalAddress<<endl;
		//cout<<"wordBitWidth is "<<wordBitWidth<<endl;
		tempB = physicalAddress << wordBitWidth;
		//cout<<"tempB we're oring with "<<tempB<<endl;
		word = tempA ^ tempB;
		
		
		//cout<<"we're checking the word and its "<<word<<endl;
	}else{
	  word = 0;
	  offset = log2(NV_PAGE_SIZE);
	  physicalAddress = physicalAddress >> offset;
	}

	tempA = physicalAddress;
	physicalAddress = physicalAddress >> pageBitWidth;
	tempB = physicalAddress << pageBitWidth;
	page = tempA ^ tempB;

	tempA = physicalAddress;
	physicalAddress = physicalAddress >> blockBitWidth;
	tempB = physicalAddress << blockBitWidth;
	block = tempA ^ tempB;

	tempA = physicalAddress;
	physicalAddress = physicalAddress >> planeBitWidth;
	tempB = physicalAddress << planeBitWidth;
	plane = tempA ^ tempB;

	tempA = physicalAddress;
	physicalAddress = physicalAddress >> dieBitWidth;
	tempB = physicalAddress << dieBitWidth;
	die = tempA ^ tempB;
	
	tempA = physicalAddress;
	physicalAddress = physicalAddress >> packageBitWidth;
	tempB = physicalAddress << packageBitWidth;
	package = tempA ^ tempB;

        //cout<<type<<" "<<package<<" "<<die<<" "<<plane<<" "<<block<<" "<<" "<<page<<" "<<word<<endl;

	if(DEVICE_TYPE == "NAND"){
	  if(type == READ){
	    size = NV_PAGE_SIZE*1024;
	    if(READ_SIZE != NV_PAGE_SIZE){
	      ERROR("Invalid read size of "<<READ_SIZE<<" attempted for NAND Flash, using page read instead");
	    }
	  }else if(type == WRITE){
	    size = NV_PAGE_SIZE*1024;
	    if(WRITE_SIZE != NV_PAGE_SIZE){
	      ERROR("Invalid write size of "<<WRITE_SIZE<<" attempted for NAND Flash, using page write instead");
	    }
	  }else if(type == ERASE){
	    size = BLOCK_SIZE;
	  }else{
	    size = NV_PAGE_SIZE*1024;
	  }
	}else if(DEVICE_TYPE == "NOR"){
	  if(type == READ){
	    size = NV_WORD_SIZE;
	    if(READ_SIZE != NV_WORD_SIZE){
	      ERROR("Invalid read size of "<<READ_SIZE<<" attempted for NOR Flash, using word read instead");
	    }
	  }else if(type == WRITE){
	    size = NV_PAGE_SIZE;
	    if(WRITE_SIZE != NV_PAGE_SIZE){
	      ERROR("Invalid write size of "<<WRITE_SIZE<<" attempted for NOR Flash, using page write instead");
	    }
	  }else if(type == ERASE){
	    size = BLOCK_SIZE;
	  }else{
	    size = NV_PAGE_SIZE*1024;
	  }
	}else if(DEVICE_TYPE == "PCM"){
	  if(type == READ){
	    size = READ_SIZE;
	  }else if(type == WRITE){
	    size = WRITE_SIZE;
	  }else{
	    size = NV_PAGE_SIZE*1024;
	  }
	}else if(DEVICE_TYPE == "Memristor"){
	  if(type == READ){
	    size = READ_SIZE;
	  }else if(type == WRITE){
	     size = WRITE_SIZE;
	  }else{
	    size = NV_PAGE_SIZE*1024;
	  }
	}else{
	  size = NV_PAGE_SIZE*1024;
	}

	return new ChannelPacket(type, addr, addressMap[addr], size, word, page, block, plane, die, package, NULL);
}

bool SmallAccessFtl::addTransaction(FlashTransaction &t){
	transactionQueue.push_back(t);
	return true;
}

void SmallAccessFtl::update(void){
        uint64_t block, page, word, start;

	// Decrement block erase counters
	for (std::unordered_map<uint64_t,uint64_t>::iterator it = erase_counter.begin(); it != erase_counter.end(); it++) {

		// Decrement the counter.
		uint64_t cnt = (*it).second;
		cnt--;
		(*it).second = cnt;

		// Check to see if done.
		if (cnt == 0) {
			// Set all the bits in the page to be clean.
			block = (*it).first;
			if(DEVICE_TYPE != "NAND"){
			  for (page = 0; page < PAGES_PER_BLOCK; page++) {
			    for (word = 0; word < WORDS_PER_PAGE; word++){
				used[block][page][word] = false;
				used_page_count--;
#if !PCM
				dirty[block][page][word] = false;
#endif
			    }
			  }
			}else{
			  for (page = 0; page < PAGES_PER_BLOCK; page++) {
				used[block][page][0] = false;
				used_page_count--;
				dirty[block][page][0] = false;
			  }
			}

			// Remove from erase counter map.
			erase_counter.erase(it);
		}
	}

	if (busy) {
		if (lookupCounter == 0){
			uint64_t vAddr = currentTransaction.address, pAddr;
			bool done = false;
			ChannelPacket *commandPacket, *dataPacket;

			switch (currentTransaction.transactionType){
				case DATA_READ:
					if (addressMap.find(vAddr) == addressMap.end()){
						controller->returnReadData(FlashTransaction(RETURN_DATA, vAddr, (void *)0xdeadbeef));
					} else {
						commandPacket = Ftl::translate(READ, vAddr);
						controller->addPacket(commandPacket);
					}
					break;
				case DATA_WRITE:
					if (addressMap.find(vAddr) != addressMap.end()){
					  if(DEVICE_TYPE != "NAND"){
#if PCM
					     used[addressMap[vAddr] / BLOCK_SIZE][(addressMap[vAddr] / NV_PAGE_SIZE) % PAGES_PER_BLOCK]
					      [(addressMap[vAddr] / (NV_WORD_SIZE)) % WORDS_PER_PAGE]= false;
#else
					    dirty[addressMap[vAddr] / BLOCK_SIZE][(addressMap[vAddr] / NV_PAGE_SIZE) % PAGES_PER_BLOCK]
					      [(addressMap[vAddr] / (NV_WORD_SIZE)) % WORDS_PER_PAGE]= true;
#endif
					  }else{
					    dirty[addressMap[vAddr] / BLOCK_SIZE][(addressMap[vAddr] / NV_PAGE_SIZE) % PAGES_PER_BLOCK][0] = true;
					  }
					}

				        if(DEVICE_TYPE != "NAND"){
					  //look for first free physical page starting at the write pointer
					  start = ((NV_WORD_SIZE*WORDS_PER_PAGE)/1024) * PAGES_PER_BLOCK * BLOCKS_PER_PLANE * (plane + PLANES_PER_DIE * 
						  (die + NUM_PACKAGES * channel));//yuck!
					  cout<<"start was "<<start<<endl;
					  
					  for (block = start / BLOCK_SIZE ; block < TOTAL_SIZE / BLOCK_SIZE && !done; block++){
					    for (page = 0 ; page < PAGES_PER_BLOCK  && !done ; page++){
					      for (word = 0; word < WORDS_PER_PAGE && !done; word++){
							if (!used[block][page][word]){
							        pAddr = (block * BLOCK_SIZE + page * NV_PAGE_SIZE + word * (NV_WORD_SIZE));
								//cout<<"pAddr was" <<pAddr<<endl;
								for(uint i = word; i < (word+(WRITE_SIZE/NV_WORD_SIZE)); i++){
								  used[block][page][i] = true;
								  cout<<"block "<<block<<" page "<<page<<" word "<<i<<" are being used"<<endl;
								}
								used_page_count++;
								done = true;
							}
					      }
					    }
					  }
					}else{					  
					  //look for first free physical page starting at the write pointer
					  start = NV_PAGE_SIZE * PAGES_PER_BLOCK * BLOCKS_PER_PLANE * (plane + PLANES_PER_DIE * 
							(die + NUM_PACKAGES * channel));//yuck!

					  for (block = start / BLOCK_SIZE ; block < TOTAL_SIZE / BLOCK_SIZE && !done; block++){
					    for (page = 0 ; page < PAGES_PER_BLOCK  && !done ; page++){
							if (!used[block][page][0]){
							        pAddr = (block * BLOCK_SIZE + page * NV_PAGE_SIZE);
								used[block][page][0] = true;
								used_page_count++;
								done = true;
							}
					    }
					  }
					}


					//if we didn't find a free page after scanning til the end, check the beginning
					if(DEVICE_TYPE != "NAND"){
					  if (!done){
					    for (block = 0 ; block < start / BLOCK_SIZE && !done ; block++){
					      for (page = 0 ; page < PAGES_PER_BLOCK && !done ; page++){
						for (word = 0; word < WORDS_PER_PAGE && !done; word++){
								if (!used[block][page][word]){
								  pAddr = (block * BLOCK_SIZE + page * NV_PAGE_SIZE + word * NV_WORD_SIZE);
									for(uint i = word; i < (word+(WRITE_SIZE/NV_WORD_SIZE)); i++){
									  used[block][page][i] = true;
									}
									used_page_count++;
									done = true;
								}
						}
					      }
					    }
					  }
					}else{
					  if (!done){
					    for (block = 0 ; block < start / BLOCK_SIZE && !done ; block++){
					      for (page = 0 ; page < PAGES_PER_BLOCK && !done ; page++){
								if (!used[block][page][0]){
								  pAddr = (block * BLOCK_SIZE + page * NV_PAGE_SIZE);
									used[block][page][0] = true;
									used_page_count++;
									done = true;
								} 
					      }
					    }
					  }								
					}

					if (!done){
						// TODO: Call GC
						ERROR("No free pages? GC needs some work.");
						exit(1);
					} else {
						addressMap[vAddr] = pAddr;
					}
					//send write to controller
					dataPacket = Ftl::translate(DATA, vAddr);
					commandPacket = Ftl::translate(WRITE, vAddr);
					controller->addPacket(dataPacket);
					controller->addPacket(commandPacket);
					//update "write pointer"
					channel = (channel + 1) % NUM_PACKAGES;
					if (channel == 0){
						die = (die + 1) % DIES_PER_PACKAGE;
						if (die == 0)
							plane = (plane + 1) % PLANES_PER_DIE;
					}
					break;

				case BLOCK_ERASE:
#if PCM
				        ERROR("Called Block erase on PCM memory which does not need this");
					break;
#else
					// Note: For this command, vAddr refers to the block number to erase.
					erase_counter[vAddr] = 1000000; // Initially hardcoded as 1.5 ms.
					break;
#endif
				default:
					ERROR("Transaction in Ftl that isn't a read or write... What?");
					exit(1);
					break;
			}
			transactionQueue.pop_front();
			busy = 0;
			lookupCounter = -1;
		} 
		else
			lookupCounter--;
	} // if busy
	else {
		// Not currently busy.

		if (!transactionQueue.empty()) {
			busy = 1;
			currentTransaction = transactionQueue.front();
			lookupCounter = LOOKUP_TIME;
		}
		// Should not need to do garbage collection for PCM
#if !PCM
		else {
			// Check to see if GC needs to run.
			if (checkGC()) {
				// Run the GC.
				runGC();
			}
		}
#endif	
	}
}

bool SmallAccessFtl::checkGC(void){
	//uint64_t block, page, count = 0;

	// Count the number of blocks with used pages.
	//for (block = 0; block < TOTAL_SIZE / BLOCK_SIZE; block++) {
	//	for (page = 0; page < PAGES_PER_BLOCK; page++) {
	//		if (used[block][page] == true) {
	//			count++;
	//			break;
	//		}
	//	}
	//}
	
	// Return true if more than 70% of pagess are in use and false otherwise.
	if (((float)used_page_count / TOTAL_SIZE) > 0.7)
		return true;
	else
		return false;
}


void SmallAccessFtl::runGC(void) {
  uint64_t block, page, word, count, dirty_block=0, dirty_count=0, pAddr, vAddr, tmpAddr;
	FlashTransaction trans;

	// Get the dirtiest block (assumes the flash keeps track of this with an online algorithm).
	if(DEVICE_TYPE != "NAND"){
	  for (block = 0; block < TOTAL_SIZE / BLOCK_SIZE; block++) {
		count = 0;
		for (page = 0; page < PAGES_PER_BLOCK; page++) {
		  for(word = 0; word < WORDS_PER_PAGE; word++) {
			if (dirty[block][page][word] == true) {
				count++;
			}
		  }
		}
		if (count > dirty_count) {
			dirty_count = count;
			dirty_block = block;
		}
	  }
	}else{
	  for (block = 0; block < TOTAL_SIZE / BLOCK_SIZE; block++) {
		count = 0;
		for (page = 0; page < PAGES_PER_BLOCK; page++) {
			if (dirty[block][page][0] == true) {
				count++;
			}
		}
		if (count > dirty_count) {
			dirty_count = count;
			dirty_block = block;
		}
	  }
	}

	// All used pages in the dirty block, they must be moved elsewhere.
	if(DEVICE_TYPE != "NAND"){
	  for (page = 0; page < PAGES_PER_BLOCK; page++) {
	    for (word = 0; word < WORDS_PER_PAGE; word++) {
		if (used[dirty_block][page][word] == true && dirty[dirty_block][page][word] == false) {
			// Compute the physical address to move.
			pAddr = (dirty_block * BLOCK_SIZE + page * NV_PAGE_SIZE + word * NV_WORD_SIZE);

			// Do a reverse lookup for the virtual page address.
			// This is slow, but the alternative is maintaining a full reverse lookup map.
			// Another alternative may be to make new FlashTransaction commands for physical address read/write.
			bool found = false;
			for (std::unordered_map<uint64_t, uint64_t>::iterator it = addressMap.begin(); it != addressMap.end(); it++) {
				tmpAddr = (*it).second;
				if (tmpAddr == pAddr) {
					vAddr = (*it).first;
					found = true;
					break;
				}
			}
			assert(found);
			

			// Schedule a read and a write.
			trans = FlashTransaction(DATA_READ, vAddr, NULL);
			addTransaction(trans);
			trans = FlashTransaction(DATA_WRITE, vAddr, NULL);
			addTransaction(trans);
		}
	    }
	  }
	}else{
	  for (page = 0; page < PAGES_PER_BLOCK; page++) {
		if (used[dirty_block][page][0] == true && dirty[dirty_block][page][0] == false) {
			// Compute the physical address to move.
			pAddr = (dirty_block * BLOCK_SIZE + page * NV_PAGE_SIZE) * 1024;

			// Do a reverse lookup for the virtual page address.
			// This is slow, but the alternative is maintaining a full reverse lookup map.
			// Another alternative may be to make new FlashTransaction commands for physical address read/write.
			bool found = false;
			for (std::unordered_map<uint64_t, uint64_t>::iterator it = addressMap.begin(); it != addressMap.end(); it++) {
				tmpAddr = (*it).second;
				if (tmpAddr == pAddr) {
					vAddr = (*it).first;
					found = true;
					break;
				}
			}
			assert(found);
			

			// Schedule a read and a write.
			trans = FlashTransaction(DATA_READ, vAddr, NULL);
			addTransaction(trans);
			trans = FlashTransaction(DATA_WRITE, vAddr, NULL);
			addTransaction(trans);
		}
	  }
	}

	// Schedule the BLOCK_ERASE command.
	// Note: The address field is just the block number, not an actual byte address.
	trans = FlashTransaction(BLOCK_ERASE, dirty_block, NULL);
	addTransaction(trans);

}

uint64_t SmallAccessFtl::get_ptr(void) {
    // Return a pointer to the current plane.
    return NV_PAGE_SIZE * PAGES_PER_BLOCK * BLOCKS_PER_PLANE * 
	   (plane + PLANES_PER_DIE * (die + NUM_PACKAGES * channel));
}

#endif