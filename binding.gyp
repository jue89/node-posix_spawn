{
	"targets": [{
		"target_name": "posix_spawn",
		"sources": ["src/posix_spawn.cc"],
		"include_dirs": ["<!(node -e \"require('nan')\")"],
		"libraries": []
	}]
}
