function cm { & cmake $args }
function cpd { & cmake --preset default -B build         $args }
function cpr { & cmake --preset release -B build_release $args }
function cbb { & cmake --build build $args }
function cbr { & cmake --build build_release $args }
