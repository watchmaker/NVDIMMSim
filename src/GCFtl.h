#ifndef GCFTL_H
#define GCFTL_H
//GCFtl.h
//header file for the ftl with garbage collection

#include <iostream>
#include <fstream>
#include "SimObj.h"
#include "FlashConfiguration.h"
#include "ChannelPacket.h"
#include "FlashTransaction.h"
#include "Controller.h"
#include "Ftl.h"

namespace NVDSim{
	class GCFtl : public Ftl{
		public:
			GCFtl(Controller *c);
			bool addTransaction(FlashTransaction &t);
			void update(void);
			bool checkGC(void); 
			void runGC(void);

			void saveStats(uint64_t cycle, uint64_t reads, uint64_t writes, uint64_t erases);
			void printStats(uint64_t cycle, uint64_t reads, uint64_t writes, uint64_t erases);
			void powerCallback(void);

			//Accessors for power data
			//Writing correct object oriented code up in this piece, what now?
			vector<double> getEraseEnergy(void);

		protected:
			std::vector<vector<bool>> dirty;

			// Power Stuff
			// This is computed per package
			std::vector<double> erase_energy;
	};
}
#endif
