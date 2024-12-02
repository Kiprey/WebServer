create table user_info
(
    userid bigserial,
    deviceid varchar(36) unique,

    constraint pk_user_info primary key (userid)
);