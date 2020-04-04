/*
This file is part of Darling.

Copyright (C) 2020 Lubos Dolezel

Darling is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Darling is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Darling.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef AUDIOHARDWAREIMPLPA_H
#define AUDIOHARDWAREIMPLPA_H
#include "../AudioHardwareImpl.h"
#include "PADispatchMainLoop.h"
#include <memory>

class PADispatchMainLoop;

class AudioHardwareImplPA : public AudioHardwareImpl
{
public:
	AudioHardwareImplPA(AudioObjectID myId, const char* paRole = nullptr);
	~AudioHardwareImplPA();

	OSStatus getPropertyData(const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize,
		const void* inQualifierData, UInt32* ioDataSize, void* outData) override;
	
	OSStatus setPropertyData(const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize,
		const void* inQualifierData, UInt32 inDataSize, const void* inData) override;
	
	void getPAContext(void (^cb)(pa_context*));
	static pa_sample_spec paSampleSpecForASBD(const AudioStreamBasicDescription& asbd, bool* convertSignedUnsigned = nullptr);
protected:
	AudioHardwareStream* createStream(AudioDeviceIOProc callback, void* clientData) override;
	bool validateFormat(const AudioStreamBasicDescription* asbd) const override;
private:
	pa_context* m_context = nullptr;
	std::unique_ptr<PADispatchMainLoop> m_loop;
	const char* m_paRole;
};

#endif

