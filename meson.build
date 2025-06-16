project('AudioUnitSDK', 'c', 'cpp')

sources = [
    'src/AudioUnitSDK/AUBase.cpp',
    'src/AudioUnitSDK/AUBuffer.cpp',
    'src/AudioUnitSDK/AUBufferAllocator.cpp',
    'src/AudioUnitSDK/AUEffectBase.cpp',
    'src/AudioUnitSDK/AUInputElement.cpp',
    'src/AudioUnitSDK/AUMIDIBase.cpp',
    'src/AudioUnitSDK/AUMIDIEffectBase.cpp',
    'src/AudioUnitSDK/AUOutputElement.cpp',
    'src/AudioUnitSDK/AUPluginDispatch.cpp',
    'src/AudioUnitSDK/AUScopeElement.cpp',
    'src/AudioUnitSDK/ComponentBase.cpp',
    'src/AudioUnitSDK/MusicDeviceBase.cpp'
]

audiounitsdk_incdirs = include_directories('include')

audiounitsdk_lib = static_library('AudioUnitSDK', sources,
    include_directories: audiounitsdk_incdirs)

audiounitsdk_dep = declare_dependency(include_directories : 'include',
	link_with : audiounitsdk_lib)

meson.override_dependency('AudioUnitSDK', audiounitsdk_dep)

