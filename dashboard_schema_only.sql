--
-- PostgreSQL database schema for Fox GPU Testing Dashboard
-- Clean version without sample data
--

SET statement_timeout = 0;
SET lock_timeout = 0;
SET idle_in_transaction_session_timeout = 0;
SET transaction_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SELECT pg_catalog.set_config('search_path', '', false);
SET check_function_bodies = false;
SET xmloption = content;
SET client_min_messages = warning;
SET row_security = off;

--
-- TOC entry 7 (class 2615 OID 16387)
-- Name: pgagent; Type: SCHEMA; Schema: -; Owner: postgres
--

CREATE SCHEMA pgagent;

ALTER SCHEMA pgagent OWNER TO postgres;

--
-- TOC entry 4936 (class 0 OID 0)
-- Dependencies: 7
-- Name: SCHEMA pgagent; Type: COMMENT; Schema: -; Owner: postgres
--

COMMENT ON SCHEMA pgagent IS 'pgAgent system tables';

--
-- TOC entry 2 (class 3079 OID 16388)
-- Name: pgagent; Type: EXTENSION; Schema: -; Owner: -
--

CREATE EXTENSION IF NOT EXISTS pgagent WITH SCHEMA pgagent;

--
-- TOC entry 4937 (class 0 OID 0)
-- Dependencies: 2
-- Name: EXTENSION pgagent; Type: COMMENT; Schema: -; Owner: 
--

COMMENT ON EXTENSION pgagent IS 'A PostgreSQL job scheduler';

SET default_tablespace = '';

SET default_table_access_method = heap;

--
-- TOC entry 239 (class 1259 OID 24750)
-- Name: fixtures; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.fixtures (
    id integer NOT NULL,
    tester_type character varying(32),
    fixture_id character varying(32),
    rack character varying(32),
    fixture_sn character varying(32),
    test_type character varying(32),
    ip_address character varying(16),
    mac_address character varying(32),
    parent integer,
    creator character varying(32),
    create_date timestamp with time zone,
    CONSTRAINT fixtures_test_type_check CHECK ((((test_type)::text = 'Refurbish'::text) OR ((test_type)::text = 'Sort'::text) OR ((test_type)::text = 'NA'::text))),
    CONSTRAINT fixtures_tester_type_check CHECK ((((tester_type)::text = 'B Tester'::text) OR ((tester_type)::text = 'LA Slot'::text) OR ((tester_type)::text = 'RA Slot'::text)))
);

ALTER TABLE public.fixtures OWNER TO postgres;

--
-- TOC entry 238 (class 1259 OID 24749)
-- Name: fixtures_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

ALTER TABLE public.fixtures ALTER COLUMN id ADD GENERATED ALWAYS AS IDENTITY (
    SEQUENCE NAME public.fixtures_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1
);

--
-- TOC entry 241 (class 1259 OID 24763)
-- Name: health; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.health (
    primary_key integer NOT NULL,
    fixture_id integer,
    status character varying(32),
    comments character varying(256),
    creator character varying(32),
    create_date timestamp with time zone,
    CONSTRAINT health_status_check CHECK ((((status)::text = 'active'::text) OR ((status)::text = 'no_response'::text) OR ((status)::text = 'under_maintenance'::text) OR ((status)::text = 'RMA'::text)))
);

ALTER TABLE public.health OWNER TO postgres;

--
-- TOC entry 240 (class 1259 OID 24762)
-- Name: health_primary_key_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

ALTER TABLE public.health ALTER COLUMN primary_key ADD GENERATED ALWAYS AS IDENTITY (
    SEQUENCE NAME public.health_primary_key_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1
);

--
-- TOC entry 243 (class 1259 OID 24775)
-- Name: usage; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.usage (
    primary_key integer NOT NULL,
    fixture_id integer,
    test_slot character varying(32),
    test_station character varying(256),
    test_type character varying(32),
    gpu_pn character varying(32),
    gpu_sn character varying(32),
    log_path character varying(256),
    creator character varying(32),
    create_date timestamp with time zone,
    CONSTRAINT usage_test_slot_check CHECK ((((test_slot)::text = 'Left'::text) OR ((test_slot)::text = 'Right'::text))),
    CONSTRAINT usage_test_type_check CHECK ((((test_type)::text = 'Refurbish'::text) OR ((test_type)::text = 'Sort'::text) OR ((test_type)::text = 'NA'::text)))
);

ALTER TABLE public.usage OWNER TO postgres;

--
-- TOC entry 242 (class 1259 OID 24774)
-- Name: usage_primary_key_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

ALTER TABLE public.usage ALTER COLUMN primary_key ADD GENERATED ALWAYS AS IDENTITY (
    SEQUENCE NAME public.usage_primary_key_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1
);

--
-- TOC entry 4772 (class 2606 OID 24756)
-- Name: fixtures fixtures_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.fixtures
    ADD CONSTRAINT fixtures_pkey PRIMARY KEY (id);

--
-- TOC entry 4774 (class 2606 OID 24768)
-- Name: health health_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.health
    ADD CONSTRAINT health_pkey PRIMARY KEY (primary_key);

--
-- TOC entry 4776 (class 2606 OID 24783)
-- Name: usage usage_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.usage
    ADD CONSTRAINT usage_pkey PRIMARY KEY (primary_key);

--
-- TOC entry 4777 (class 2606 OID 24757)
-- Name: fixtures fixtures_parent_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.fixtures
    ADD CONSTRAINT fixtures_parent_fkey FOREIGN KEY (parent) REFERENCES public.fixtures(id);

--
-- TOC entry 4778 (class 2606 OID 24769)
-- Name: health health_fixture_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.health
    ADD CONSTRAINT health_fixture_id_fkey FOREIGN KEY (fixture_id) REFERENCES public.fixtures(id);

--
-- TOC entry 4779 (class 2606 OID 24784)
-- Name: usage usage_fixture_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.usage
    ADD CONSTRAINT usage_fixture_id_fkey FOREIGN KEY (fixture_id) REFERENCES public.fixtures(id);

-- Completed on 2025-09-24 15:31:45

--
-- PostgreSQL database schema complete
--
