package My::Suite::DiskSizeInfo;

@ISA= qw(My::Suite);

return "No disk_size_info plugin" unless $ENV{DISK_SIZE_INFO_SO};

sub is_default { 1 }

bless {};
