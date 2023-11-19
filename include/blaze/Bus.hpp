#pragma once

#include <blaze/CPU.hpp>
#include <blaze/MemRam.hpp>
#include <blaze/ROM.hpp>
#include <blaze/MMIO.hpp>

namespace Blaze
{
	struct BusInterface {
		~BusInterface() = default;

		virtual void write(Address addr, Byte data) = 0; 	// write8
		virtual void write(Address addr, Word data) = 0; 	// write16
		virtual void write(Address addr, Address data) = 0; // write 24
		virtual Byte read8(Address addr) = 0;
		virtual Word read16(Address addr) = 0;
		virtual Address read24(Address addr) = 0;
	};

	struct Bus: public BusInterface
	{
		//=== Devices connected to the bus ===
		CPU cpu;
		MemRam ram;
		ROM rom;

		//=== Constructor & Destructor ===
		Bus();

		//=== Bus Functionality ===
		void write(Address addr, Byte data) override;
		void write(Address addr, Word data) override;
		void write(Address addr, Address data) override;
		Byte read8(Address addr) override;
		Word read16(Address addr) override;
		Address read24(Address addr) override;

		void reset();

	private:
		void findDeviceAndOffset(Address address, MMIODevice*& outDevice, Address& outOffset);
	};
}
